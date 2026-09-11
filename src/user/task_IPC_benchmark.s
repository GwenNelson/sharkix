.text
.code64
.global _start

#define SYS_TEST_BENCHMARK 4
#define SYS_IPC_SEND       -202
#define SYS_IPC_RECV       -203
#define SYS_TEST_EXIT      1
#define BENCH_READY        0
#define BENCH_START        1
#define BENCH_STOP         2
#define WARMUP_ROUNDS      100
#define MEASURE_ROUNDS     10000

_start:
    popq %r12
    popq %r13
    popq %r14
    movq $SYS_TEST_BENCHMARK, %rax
    movq $BENCH_READY, %rdi
    syscall
    xorq %rbx, %rbx
warmup:
    movq $SYS_IPC_RECV, %rax
    movq %r12, %rdi
    syscall
    testq %rax, %rax
    jne exit
    incq %rbx
    cmpq $WARMUP_ROUNDS, %rbx
    jne warmup_forward
    cmpq $0, -8(%rsp)
    jne warmup_done
    movq $SYS_TEST_BENCHMARK, %rax
    movq $BENCH_START, %rdi
    syscall
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    syscall
    xorq %rbx, %rbx
    jmp measured_wait
warmup_done:
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    syscall
    xorq %rbx, %rbx
    jmp measured_wait
warmup_forward:
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    syscall
    jmp warmup
measured_wait:
    movq $SYS_IPC_RECV, %rax
    movq %r12, %rdi
    syscall
    testq %rax, %rax
    jne exit
    incq %rbx
    cmpq $MEASURE_ROUNDS, %rbx
    je measured_stop
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    syscall
    jmp measured_wait
measured_stop:
    cmpq $0, -8(%rsp)
    jne measured_continue
    movq $SYS_TEST_BENCHMARK, %rax
    movq $BENCH_STOP, %rdi
    syscall
    jmp exit
measured_continue:
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    syscall
    xorq %rbx, %rbx
    jmp measured_wait
exit:
    movq $SYS_TEST_EXIT, %rax
    syscall
1:  jmp 1b
