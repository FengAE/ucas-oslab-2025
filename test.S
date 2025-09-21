
.global main
.equ ADDRESS, 0x60001000
main:
    li      t0, 1        
    li      t1, 50       
    li      t2, 0        ; sum = 0

Loop:
    bgt     t0, t1, done  
    add     t2, t2, t0     
    addi    t0, t0, 1    
    jal     x0, Loop       

done:
    lui     t3, %hi(ADDRESS)     
    addi    t3, t3, %lo(ADDRESS)
    sw      t2, 0(t3)             
    ret
