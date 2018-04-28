// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & FEC_WR)) {
		panic("pgfault: not fault-on-write\n");
	}
	pte_t pte = *((pte_t*) (UVPT + 4 * PGNUM(addr)));
	if (!(pte & PTE_COW)) {
		panic("pgfault: page not set to copy-on-write for addr %08x\n", addr);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	r = sys_page_alloc(0, PFTEMP, PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("pgfault: sys_page_alloc return %e", r);
	}

	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("pgfault: sys_page_map return %e", r);
	}

	r = sys_page_unmap(0, PFTEMP);
	if (r < 0) {
		panic("pgfault: sys_page_unmap return %e", r);
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn) {
	int r;

	// LAB 4: Your code here.
	pte_t pte = uvpt[pn];
	void *va = (void*)(pn * PGSIZE);
	if (pte & PTE_SHARE) {
		if ((r = sys_page_map(0, va, envid, va, pte & PTE_SYSCALL)) < 0)
			panic("duppage: sys_page_map return %e", r);
	} else if (pte & PTE_COW || pte & PTE_W) {
		if ((r = sys_page_map(0, va, envid, va, PTE_COW | PTE_U | PTE_P)) < 0) {
			panic("duppage: sys_page_unmap return %e", r);
		}
        if ((r = sys_page_map(0, va, 0, va, PTE_COW | PTE_U | PTE_P)) < 0) {
            panic("duppage: sys_page_unmap return %e", r);
        }
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
    set_pgfault_handler(pgfault);
	int r;
	envid_t envid = sys_exofork();
	if (envid < 0) {
		panic("fork: sys_exofork: %e", envid);
	}
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	uint8_t *va;

	for (va = (uint8_t *) UTEXT; (uintptr_t)va < USTACKTOP; va += PGSIZE) {
        if ((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P) &&
            (uvpt[PGNUM(va)] & PTE_U)) {
            duppage(envid, PGNUM(va));
        }
	}

	if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE),
							PTE_U | PTE_W | PTE_P)) < 0) {
		panic("fork: sys_page_alloc: %e", r);
	}

	if ((r = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall)) < 0) {
		panic("fork: sys_env_set_pgfault_upcall: %e", r);
	}

	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) {
		panic("fork: sys_env_set_status: %e", r);
	}

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
