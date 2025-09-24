.section .data
ADDRESS: .word 0        
.section .text
.global main
main:
    li      t0, 1        
    li      t1, 50       
    li      t2, 0        # sum = 0

Loop:
    bgt     t0, t1, done  
    add     t2, t2, t0     
    addi    t0, t0, 1    
    jal     x0, Loop       

done:
    la      t3, ADDRESS # t3: ADDRESS
    sw      t2, 0(t3)             
    ret
