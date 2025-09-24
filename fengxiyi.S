.global main
main:
    li      t0, 1        
    li      t4, 200       
    li      t2, 0        # sum = 0
    li      sp, 0x50020000  # else sp intial as 0x0, error

Loop:
    bgt     t0, t4, Done  
    mv      a0, t0
    call    Judge_prime # a0: is prime or not

    addi    t0, t0, 1
    jal     x0, Loop       

Done:
    ret



# ------ judge fun -----------
Judge_prime:
    addi    sp, sp, -16   
    sw      ra, 8(sp)       
    sw      s0, 0(sp)       
    mv      s0, sp       

    # n==1: not prime
    li      t3, 1
    beq     a0, t3, Not_prime

    mv      t0, a0  # t0: n
    li      t1, 2   # t1: i=2

Loop1:
    mul     t3, t1, t1  # t3: i²
    bgt     t3, t0, Done_loop1  # i²>n

#---- loop sub to judge is n%i==0 ----
    mv      t2, t0  # t2: n
Loop_sub:
    ble     t2, zero, Done_sub
    sub     t2, t2, t1 
    jal     x0, Loop_sub

Done_sub:
    # t2==0 --> i is factor: not prime
    beq     t2, zero, Not_prime
    # t2!=0 --> i is not factor
    addi    t1, t1, 1   # i++
    jal     x0, Loop1
# ------------------------------------
    li      a0, 1
    jal     x0, Fun_ret

Done_loop1:
    li      a0, 1

Fun_ret:
    lw      s0, 0(sp) 
    lw      ra, 8(sp)            
    addi    sp, sp, 16    
    ret

Not_prime:
    li      a0, 0
    jal     x0, Fun_ret