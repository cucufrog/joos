// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "btrace", "Display stack backtrace", mon_backtrace },
	{ "pgdir", "Display page directory", mon_pgdir },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

static void 
__mon_help(void)
{
    int i;
    for (i = 0; i < NCOMMANDS; i++)
        cprintf("* %s - %s\n", commands[i].name, commands[i].desc);
}
int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
        __mon_help();
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

void
print_backtrace(void){
        int i;
        uint32_t *ebp, *eip;
        struct Eipdebuginfo info;

        ebp = (uint32_t *)read_ebp();
        eip = (uint32_t *)read_eip();

	// Your code here.
        cprintf("===== Stack Backrace =====\n");

        while (ebp != NULL) {
            // print filename:line number: <function name>
            if (debuginfo_eip((uintptr_t)eip, &info) == -1) {
                cprintf("error!!\n");
            }
            cprintf("%s:%d: %.*s", 
                    info.eip_file, 
                    info.eip_line, 
                    info.eip_fn_namelen, info.eip_fn_name);

            // print (args)
            cprintf("(");
            if (info.eip_fn_narg != 0) {
                for ( i=1; i<=info.eip_fn_narg; ++i){
                    if (i != 1) cprintf(" ,");
                    cprintf("%x", *(ebp-i));
                }
            }
            cprintf(")");

            // print ebp, eip and args
            cprintf(" <ebp:%x, eip:%x>", (uint32_t)ebp, (uint32_t)eip);
            cprintf("\n");

            
            ebp = (uint32_t *)*ebp;
            eip = (uint32_t *)*(ebp + 1);

        }

        cprintf("===== Backtrace End =====\n");

}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
        print_backtrace();
	return 0;
}

void 
print_pgdir()
{
    //pde_t *kern_pgdir = (pde_t *) UVPT;
    extern pde_t *kern_pgdir;
    size_t pdx, ptx;

    //kern_pgdir = (pde_t *)PADDR(kern_pgdir);
    cprintf(" pdx\t va\tpa\t\tperm\n");
    for (pdx=0; pdx<NPDENTRIES; ++pdx) {
        pde_t pgtbl = kern_pgdir[pdx];
        if (pgtbl & PTE_P) {
            uintptr_t va = pdx << 22;
            physaddr_t pa = PTE_ADDR(pgtbl);
            size_t perm = pgtbl & 0xfff;
            cprintf("%4d\t%x\t%x\t%x\n", pdx, va, pa, perm);
            /*
            for (ptx=0; ptx<NPTENTRIES; ++ptx) {
                pte_t pte = ((pte_t *)PTE_ADDR(pgtbl))[ptx];
                if (pte & PTE_P) {
                    physaddr_t pa = PTE_ADDR(pte);
                    size_t perm = pte & 0xfff;
                    uintptr_t va = pdx << 22 | ptx << 12;
                    cprintf("%4d\t%4d\t%x\t%x\t%x\n", pdx, ptx, va, pa, perm);
                }
            }
            */
        }
    }
    
}
int
mon_pgdir(int argc, char **argv, struct Trapframe *tf)
{
        print_pgdir();
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Error: unknown command '%s'. \nAvailable commands are:\n", argv[0]);
        __mon_help();
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("*********************************************\n");
	cprintf("*     Welcome to the JOOS kernel monitor!    *\n");
	cprintf("*     Type 'help' for a list of commands.   *\n");
	cprintf("*********************************************\n");


	while (1) {
		buf = readline("joos> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
