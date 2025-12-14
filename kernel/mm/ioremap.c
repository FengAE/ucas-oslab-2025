#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>

// maybe you can map it to IO_ADDR_START ?
static uintptr_t io_base = IO_ADDR_START;

void *ioremap(unsigned long phys_addr, unsigned long size)
{
    // TODO: [p5-task1] map one specific physical region to virtual address
    uintptr_t map_start = ROUNDDOWN(phys_addr, PAGE_SIZE);
    uintptr_t map_end = ROUND(phys_addr + size, PAGE_SIZE);
    uintptr_t num_pages = (map_end - map_start) / PAGE_SIZE;
    uintptr_t offset = phys_addr - map_start;

    // 2. Allocate a virtual address block from the IO area
    uintptr_t va_start = io_base;
    io_base += num_pages * PAGE_SIZE;
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA);

    for(int i=0; i<num_pages; i++)
    {
        uintptr_t va = va_start + i*PAGE_SIZE;
        uint64_t vpn2 = (va >> 30) & 0x1FF;
        uint64_t vpn1 = (va >> 21) & 0x1FF;
        uint64_t vpn0 = (va >> 12) & 0x1FF;

        PTE *pgdir_kva = (PTE *)pgdir;
        // ---------------- Level 2 ----------------
        if ((pgdir_kva[vpn2] & _PAGE_PRESENT) == 0) 
        {
            uintptr_t new_page_kva = allocPage(1);
            set_pfn(&pgdir_kva[vpn2], kva2pa(new_page_kva) >> NORMAL_PAGE_SHIFT);   
            set_attribute(&pgdir_kva[vpn2], _PAGE_PRESENT); // not leaf: only set V
        }
        // set pte: point to next level
        PTE *pmd_kva = (PTE *)pa2kva(get_pa(pgdir_kva[vpn2]));
        // ---------------- Level 1 ----------------
        if ((pmd_kva[vpn1] & _PAGE_PRESENT)== 0) 
        {
            uintptr_t new_page_kva = allocPage(1);
            set_pfn(&pmd_kva[vpn1], kva2pa(new_page_kva) >> NORMAL_PAGE_SHIFT);
            set_attribute(&pmd_kva[vpn1], _PAGE_PRESENT);
        }

        PTE *pte_kva = (PTE *)pa2kva(get_pa(pmd_kva[vpn1]));
        // ---------------- Level 0 (Leaf) ----------------
        if ((pte_kva[vpn0] & _PAGE_PRESENT) == 0) 
        {
            // alloc data page (4KB)
            uintptr_t data_page_kva = get_user_page();
            set_pfn(&pte_kva[vpn0], kva2pa(data_page_kva) >> NORMAL_PAGE_SHIFT);

            set_attribute(&pte_kva[vpn0], 
                _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE| 
                _PAGE_ACCESSED | _PAGE_DIRTY);  // only run in kernel, and can't excute
        }
    }
    local_flush_tlb_all();
    return (void*)(va_start + offset);
}

void iounmap(void *io_addr)
{
    // TODO: [p5-task1] a very naive iounmap() is OK
    // maybe no one would call this function?
}
