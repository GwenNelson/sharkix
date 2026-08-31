.text
.code64
.global _start
_start:
    movabsq $0x737461636b2d7273, %rbx
    pushq %rbx
    movq %rsp, %r12
    movq $2, %rax
    syscall
    cmpq %r12, %rsp
    jne failure
    movabsq $0x737461636b2d7273, %rbx
    cmpq %rbx, (%rsp)
    jne failure
    addq $8, %rsp
    movabsq $0x000000000000b10c, %rbx
    cmpq %rbx, %rax
    jne failure
    movabsq $0x1111111111111111, %rbx
    cmpq %rbx, %rdi
    jne failure
    movabsq $0x2222222222222222, %rbx
    cmpq %rbx, %rsi
    jne failure
    movabsq $0x3333333333333333, %rbx
    cmpq %rbx, %rdx
    jne failure
    movabsq $0x4444444444444444, %rbx
    cmpq %rbx, %r10
    jne failure
    movabsq $0x5555555555555555, %rbx
    cmpq %rbx, %r8
    jne failure
    movabsq $0x6666666666666666, %rbx
    cmpq %rbx, %r9
    jne failure
    movq $'A', %rdi
    jmp report
failure:
    movq $'!', %rdi
report:
    movq $0, %rax
    syscall
    movq $1, %rax
    syscall
1:  jmp 1b
