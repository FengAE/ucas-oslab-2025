#include <os/mm.h>
#include <os/string.h>
#include <printk.h>

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
static ptr_t freePageList = 0;
#define MEM_START 0xffffffc050000000
#define MEM_END 0xffffffc060000000

ptr_t allocPage(int numPage)
{
    // if distribute 1 page && freelist not empty
    if (numPage == 1 && freePageList != 0) 
    {
        ptr_t ret = freePageList;
        freePageList = *(ptr_t *)ret;   // point to next
        memset((void*)ret, 0, PAGE_SIZE); 
        return ret;
    }

    // align PAGE_SIZE
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;
    memset((void*)ret, 0, numPage * PAGE_SIZE);
    return ret;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif

void freePage(ptr_t baseAddr)
{
    // TODO [P4-task1] (design you 'freePage' here if you need):
    if (baseAddr == (ptr_t)NULL) return;
    if (baseAddr < MEM_START || baseAddr >= MEM_END) return; 

    *(ptr_t *)baseAddr = freePageList;
    freePageList = baseAddr;
}

void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
    int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return (void *)allocPage(num_pages);
}


/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // TODO [P4-task1] share_pgtable:
    memcpy((void*)dest_pgdir, (void*)src_pgdir, PAGE_SIZE);
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    // TODO [P4-task1] alloc_page_helper:
    // VA: [38:30] VPN2 | [29:21] VPN1 | [20:12] VPN0 | [11:0] Offset
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
        uintptr_t data_page_kva = allocPage(1);
        set_pfn(&pte_kva[vpn0], kva2pa(data_page_kva) >> NORMAL_PAGE_SHIFT);

        set_attribute(&pte_kva[vpn0], 
            _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | 
            _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
        return data_page_kva; 
    }

    // mapping exists
    return pa2kva(get_pa(pte_kva[vpn0]));
}

static void _recycle_walker(PTE *entry, int level)
{
    if (!(*entry & _PAGE_PRESENT)) return;  // not valid
    uintptr_t next_kva = pa2kva(get_pa(*entry));

    if ((level==0) && (*entry & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) != 0) 
    {   // is leaf node
        if (*entry & _PAGE_USER) // only is user_page recycle
        {
            freePage(next_kva);
            *entry = 0;
        }
        // if kernel large page: not recycle
        return;
    }
    if (level > 0) 
    {
        PTE *child_pt = (PTE *)next_kva;
        for (int i = 0; i < NUM_PTE_ENTRY; i++) 
            _recycle_walker(&child_pt[i], level - 1);
        freePage(next_kva);
        *entry = 0;
    }
}

void recycle_page_table(uintptr_t pgdir)
{
    PTE *pgdir_entry = (PTE *)pgdir;
    // only search user space!
    for (int i = 0; i < NUM_PTE_ENTRY / 2; i++) 
    {
        _recycle_walker(&pgdir_entry[i], 2); // Level 2: root
    }
}

uintptr_t shm_page_get(int key)
{
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(uintptr_t addr)
{
    // TODO [P4-task4] shm_page_dt:
}
