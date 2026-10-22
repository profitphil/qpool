// qpool_link_stubs.cpp -- link-time replacements used only by check_contract.sh --run (macOS x86_64, SysV ABI).
// The core ships SetJump/LongJump only as Microsoft-ABI assembly; the contract executor uses them to unwind a
// contract that aborts. These are minimal SysV equivalents: callee-saved registers, stack pointer and resume address.
#include <cstdio>
#include <cstdlib>

extern "C" {
unsigned long long SetJump(void* buf);
void LongJump(void* buf, unsigned long long value);
void InternalAssertJumpBuffer(void*) {}
}

__asm__(
    ".text\n"
    ".globl _SetJump\n"
    "_SetJump:\n"
    "    movq (%rsp), %rdx\n"          // return address
    "    leaq 8(%rsp), %rcx\n"         // stack pointer after the return
    "    movq %rbx, 0(%rdi)\n"
    "    movq %rcx, 8(%rdi)\n"
    "    movq %rbp, 16(%rdi)\n"
    "    movq %r12, 24(%rdi)\n"
    "    movq %r13, 32(%rdi)\n"
    "    movq %r14, 40(%rdi)\n"
    "    movq %r15, 48(%rdi)\n"
    "    movq %rdx, 56(%rdi)\n"
    "    xorl %eax, %eax\n"
    "    ret\n"
    ".globl _LongJump\n"
    "_LongJump:\n"
    "    movq %rsi, %rax\n"
    "    movq 0(%rdi), %rbx\n"
    "    movq 8(%rdi), %rsp\n"
    "    movq 16(%rdi), %rbp\n"
    "    movq 24(%rdi), %r12\n"
    "    movq 32(%rdi), %r13\n"
    "    movq 40(%rdi), %r14\n"
    "    movq 48(%rdi), %r15\n"
    "    jmpq *56(%rdi)\n");
