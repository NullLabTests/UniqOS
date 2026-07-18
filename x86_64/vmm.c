#include "vmm.h"
#include "pmm.h"
#include "kernel.h"

static uint64_t *kernel_pml4 = 0;

#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4
#define PAGE_HUGE      0x80

#define PML4_GET(m, i) ((uint64_t*)((m)[i] & ~0xFFF))
#define PDP_GET(m, i)  ((uint64_t*)((m)[i] & ~0xFFF))
#define PD_GET(m, i)   ((uint64_t*)((m)[i] & ~0xFFF))
#define PT_GET(m, i)   ((uint64_t*)((m)[i] & ~0xFFF))

#define PML4_IDX(v) (((v) >> 39) & 0x1FF)
#define PDP_IDX(v)  (((v) >> 30) & 0x1FF)
#define PD_IDX(v)   (((v) >> 21) & 0x1FF)
#define PT_IDX(v)   (((v) >> 12) & 0x1FF)

void vmm_init(void) {
    kernel_pml4 = (uint64_t *)pmm_alloc_page();
    for (int i = 0; i < 512; i++) kernel_pml4[i] = 0;

    uint64_t phys = 0;
    for (uint64_t virt = 0; virt < 0x100000000; virt += 0x200000) {
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE);
        phys += 0x200000;
    }

    asm volatile("mov %0, %%cr3" : : "r"(kernel_pml4) : "memory");

    uint64_t cr0;
    asm("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm("mov %0, %%cr0" : : "r"(cr0) : "memory");

    kprintf("[vmm] paging enabled, PML4 at 0x%x\n", (uint64_t)kernel_pml4);
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!kernel_pml4) return -1;

    int pml4_idx = PML4_IDX(virt);
    if (!(kernel_pml4[pml4_idx] & PAGE_PRESENT)) {
        uint64_t *pdp = (uint64_t *)pmm_alloc_page();
        for (int i = 0; i < 512; i++) pdp[i] = 0;
        uint64_t pdp_addr = (uint64_t)pdp;
        kernel_pml4[pml4_idx] = pdp_addr | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    uint64_t *pdp = PDP_GET(kernel_pml4, pml4_idx);

    int pdp_idx = PDP_IDX(virt);
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) {
        uint64_t *pd = (uint64_t *)pmm_alloc_page();
        for (int i = 0; i < 512; i++) pd[i] = 0;
        uint64_t pd_addr = (uint64_t)pd;
        pdp[pdp_idx] = pd_addr | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    uint64_t *pd = PD_GET(pdp, pdp_idx);

    int pd_idx = PD_IDX(virt);

    if (flags & PAGE_HUGE) {
        pd[pd_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
        return 0;
    }
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint64_t *pt = (uint64_t *)pmm_alloc_page();
        for (int i = 0; i < 512; i++) pt[i] = 0;
        uint64_t pt_addr = (uint64_t)pt;
        pd[pd_idx] = pt_addr | PAGE_PRESENT | PAGE_WRITE;
    }
    uint64_t *pt = PT_GET(pd, pd_idx);

    int pt_idx = PT_IDX(virt);
    pt[pt_idx] = phys | (flags & ~PAGE_HUGE) | PAGE_PRESENT;

    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

uint64_t vmm_get_phys(uint64_t virt) {
    if (!kernel_pml4) return 0;
    int pml4_idx = PML4_IDX(virt);
    if (!(kernel_pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pdp = PDP_GET(kernel_pml4, pml4_idx);
    int pdp_idx = PDP_IDX(virt);
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return 0;
    if (pdp[pdp_idx] & PAGE_HUGE) {
        return (pdp[pdp_idx] & ~0x3FFFFF) + (virt & 0x3FFFFF);
    }
    uint64_t *pd = PD_GET(pdp, pdp_idx);
    int pd_idx = PD_IDX(virt);
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    if (pd[pd_idx] & PAGE_HUGE) return (pd[pd_idx] & ~0x1FFFFF) + (virt & 0x1FFFFF);
    uint64_t *pt = PT_GET(pd, pd_idx);
    int pt_idx = PT_IDX(virt);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFF) + (virt & 0xFFF);
}

void vmm_switch_page_table(uint64_t *pml4) {
    kernel_pml4 = pml4;
    asm volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
}
