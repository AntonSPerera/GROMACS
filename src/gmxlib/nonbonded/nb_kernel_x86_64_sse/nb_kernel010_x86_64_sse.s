;#
;# $Id$
;#
;# Gromacs 4.0                         Copyright (c) 1991-2003 
;# David van der Spoel, Erik Lindahl
;#
;# This program is free software; you can redistribute it and/or
;# modify it under the terms of the GNU General Public License
;# as published by the Free Software Foundation; either version 2
;# of the License, or (at your option) any later version.
;#
;# To help us fund GROMACS development, we humbly ask that you cite
;# the research papers on the package. Check out http://www.gromacs.org
;# 
;# And Hey:
;# Gnomes, ROck Monsters And Chili Sauce
;#

;# These files require GNU binutils 2.10 or later, since we
;# use intel syntax for portability, or a recent version 
;# of NASM that understands Extended 3DNow and SSE2 instructions.
;# (NASM is normally only used with MS Visual C++).
;# Since NASM and gnu as disagree on some definitions and use 
;# completely different preprocessing options I have to introduce a
;# trick: NASM uses ';' for comments, while gnu as uses '#' on x86.
;# Gnu as treats ';' as a line break, i.e. ignores it. This is the
;# reason why all comments need both symbols...
;# The source is written for GNU as, with intel syntax. When you use
;# NASM we redefine a couple of things. The false if-statement around 
;# the following code is seen by GNU as, but NASM doesn't see it, so 
;# the code inside is read by NASM but not gcc.

; .if 0    # block below only read by NASM
%define .section	section
%define .long		dd
%define .align		align
%define .globl		global
;# NASM only wants 'dword', not 'dword ptr'.
%define ptr
.equiv          .equiv                  2
   %1 equ %2
%endmacro
; .endif                   # End of NASM-specific block
; .intel_syntax noprefix   # Line only read by gnu as


;# nb010 - forces are calculated
.globl nb_kernel010_x86_64_sse
.globl _nb_kernel010_x86_64_sse
nb_kernel010_x86_64_sse:	
_nb_kernel010_x86_64_sse:	
;#	Room for return address and rbp (16 bytes)
.equiv          nb010_fshift,           16
.equiv          nb010_gid,              24
.equiv          nb010_pos,              32
.equiv          nb010_faction,          40
.equiv          nb010_charge,           48
.equiv          nb010_p_facel,          56
.equiv          nb010_argkrf,           64
.equiv          nb010_argcrf,           72
.equiv          nb010_Vc,               80
.equiv          nb010_type,             88
.equiv          nb010_p_ntype,          96
.equiv          nb010_vdwparam,         104
.equiv          nb010_Vvdw,             112
.equiv          nb010_p_tabscale,       120
.equiv          nb010_VFtab,            128
.equiv          nb010_invsqrta,         136
.equiv          nb010_dvda,             144
.equiv          nb010_p_gbtabscale,     152
.equiv          nb010_GBtab,            160
.equiv          nb010_p_nthreads,       168
.equiv          nb010_count,            176
.equiv          nb010_mtx,              184
.equiv          nb010_outeriter,        192
.equiv          nb010_inneriter,        200
.equiv          nb010_work,             208
        ;# The mutex (last arg) is not used in assembly.
	;# stack offsets for local variables  
	;# bottom of stack is cache-aligned for sse use 
.equiv          nb010_ix,               0
.equiv          nb010_iy,               16
.equiv          nb010_iz,               32
.equiv          nb010_dx,               48
.equiv          nb010_dy,               64
.equiv          nb010_dz,               80
.equiv          nb010_two,              96
.equiv          nb010_c6,               112
.equiv          nb010_c12,              128
.equiv          nb010_six,              144
.equiv          nb010_twelve,           160
.equiv          nb010_Vvdwtot,          176
.equiv          nb010_fix,              192
.equiv          nb010_fiy,              208
.equiv          nb010_fiz,              224
.equiv          nb010_half,             240
.equiv          nb010_three,            256
.equiv          nb010_nri,              272
.equiv          nb010_iinr,             280
.equiv          nb010_jindex,           288
.equiv          nb010_jjnr,             296
.equiv          nb010_shift,            304
.equiv          nb010_shiftvec,         312
.equiv          nb010_facel,            320
.equiv          nb010_innerjjnr,        328
.equiv          nb010_is3,              336
.equiv          nb010_ii3,              340
.equiv          nb010_ntia,             344
.equiv          nb010_innerk,           348
.equiv          nb010_n,                352
.equiv          nb010_nn1,              356
.equiv          nb010_ntype,            360
.equiv          nb010_nouter,           364
.equiv          nb010_ninner,           368

	push rbp
	mov  rbp, rsp
	push rbx
	
	femms

        sub rsp, 392            ; # local variable stack space (n*16+8)                                                         
	;# zero 32-bit iteration counters
	mov eax, 0
	mov [rsp + nb010_nouter], eax
	mov [rsp + nb010_ninner], eax
	
	mov edi, [rdi]
	mov [rsp + nb010_nri], edi
	mov [rsp + nb010_iinr], rsi
	mov [rsp + nb010_jindex], rdx
	mov [rsp + nb010_jjnr], rcx
	mov [rsp + nb010_shift], r8
	mov [rsp + nb010_shiftvec], r9
	mov rdi, [rbp + nb010_p_ntype]
	mov edi, [rdi]
	mov [rsp + nb010_ntype], edi

    ;# create constant floating-point factors on stack
    mov eax, 0x40000000     ;# 2.0 in IEEE (hex)
    mov [rsp + nb010_two], eax
    movss xmm1, [rsp + nb010_two]
	shufps xmm1, xmm1, 0    ;# splat to all elements
	movaps xmm2, xmm1
	addps  xmm2, xmm1       ;# 4.0
	addps  xmm2, xmm1       ;# 6.0
	movaps xmm3, xmm2
	addps  xmm3, xmm3       ;# 12.0
	movaps [rsp + nb010_two], xmm1
    movaps [rsp + nb010_six],  xmm2
    movaps [rsp + nb010_twelve], xmm3

	
.nb010_threadloop:
    mov   rsi, [rbp + nb010_count]          ;# pointer to sync counter
    mov   eax, [rsi]
.nb010_spinlock:
    mov   ebx, eax                          ;# ebx=*count=nn0
    add   ebx, 1                            ;# ebx=nn1=nn0+10
    lock cmpxchg [rsi], ebx                 ;# write nn1 to *counter,
                                            ;# if it hasnt changed.
                                            ;# or reread *counter to eax.
    pause                                   ;# -> better p4 performance
    jnz .nb010_spinlock

    ;# if(nn1>nri) nn1=nri
    mov ecx, [rsp + nb010_nri]
    mov edx, ecx
    sub ecx, ebx
    cmovle ebx, edx                         ;# if(nn1>nri) nn1=nri
    ;# Cleared the spinlock if we got here.
    ;# eax contains nn0, ebx contains nn1.
    mov [rsp + nb010_n], eax
    mov [rsp + nb010_nn1], ebx
    sub ebx, eax                            ;# calc number of outer lists
    mov esi, eax				;# copy n to esi
    jg  .nb010_outerstart
    jmp .nb010_end

.nb010_outerstart:
	;# ebx contains number of outer iterations
	add ebx, [rsp + nb010_nouter]
	mov [rsp + nb010_nouter], ebx

.nb010_outer:
    mov   rax, [rsp + nb010_shift]      	;# rax = base of shift[] 
    mov   ebx, [rax + rsi*4]            	;# ebx=shift[n] 

    lea   ebx, [ebx + ebx*2]    		;# ebx=3*is 
    mov   [rsp + nb010_is3],ebx     	;# store is3 

    mov   rax, [rsp + nb010_shiftvec]   	;# rax = base of shiftvec[] 

    movss xmm0, [rax + rbx*4]
    movss xmm1, [rax + rbx*4 + 4]
    movss xmm2, [rax + rbx*4 + 8] 

    mov   rcx, [rsp + nb010_iinr]       	;# rcx = base of iinr[] 
    mov   ebx, [rcx + rsi*4]            	;# ebx =ii 

    mov  rdx, [rbp + nb010_type] 
    mov  edx, [rdx + rbx*4]
    imul edx, [rsp + nb010_ntype]
    shl  edx, 1
    mov  [rsp + nb010_ntia], edx

    lea   ebx, [ebx + ebx*2]        	;# ebx = 3*ii=ii3 
    mov   rax, [rbp + nb010_pos]    	;# rax = base of pos[]  

    addss xmm0, [rax + rbx*4]
    addss xmm1, [rax + rbx*4 + 4]
    addss xmm2, [rax + rbx*4 + 8]

    shufps xmm0, xmm0, 0
    shufps xmm1, xmm1, 0
    shufps xmm2, xmm2, 0

    movaps [rsp + nb010_ix], xmm0
    movaps [rsp + nb010_iy], xmm1
    movaps [rsp + nb010_iz], xmm2
        
    mov   [rsp + nb010_ii3], ebx

    ;# clear Vvdwtot and i forces 
    xorps xmm4, xmm4
    movaps [rsp + nb010_Vvdwtot], xmm4
    movaps [rsp + nb010_fix], xmm4
    movaps [rsp + nb010_fiy], xmm4
    movaps [rsp + nb010_fiz], xmm4
        
    mov   rax, [rsp + nb010_jindex]
    mov   ecx, [rax  + rsi*4]    		;# jindex[n] 
    mov   edx, [rax + rsi*4 + 4]         	;# jindex[n+1] 
    sub   edx, ecx               		;# number of innerloop atoms 
        
    mov   rsi, [rbp + nb010_pos]
    mov   rdi, [rbp + nb010_faction]
    mov   rax, [rsp + nb010_jjnr]
    shl   ecx, 2
    add   rax, rcx
    mov   [rsp + nb010_innerjjnr], rax      ;# pointer to jjnr[nj0] 
	mov   ecx, edx
    sub   edx,  4
	add   ecx, [rsp + nb010_ninner]
	mov   [rsp + nb010_ninner], ecx
	add   edx, 0
    mov   [rsp + nb010_innerk], edx         ;# number of innerloop atoms 

    jge   .nb010_unroll_loop
    jmp   .nb010_finish_inner
.nb010_unroll_loop:
	;# quad-unrolled innerloop starts here 
	mov   rdx, [rsp + nb010_innerjjnr]	;# pointer to jjnr[k] 
	mov   eax, [rdx]	
	mov   ebx, [rdx + 4]              
	mov   ecx, [rdx + 8]            
	mov   edx, [rdx + 12]			;# eax-edx=jnr1-4 
	;# advance pointer (unrolled 4) 
	add   qword ptr [rsp + nb010_innerjjnr],  16 

	movd  mm0, eax		;# use mmx registers as temp storage 
	movd  mm1, ebx
	movd  mm2, ecx
	movd  mm3, edx
	
	mov rsi, [rbp + nb010_type]
	mov eax, [rsi + rax*4]
	mov ebx, [rsi + rbx*4]
	mov ecx, [rsi + rcx*4]
	mov edx, [rsi + rdx*4]
	mov rsi, [rbp + nb010_vdwparam]
	shl eax, 1	
	shl ebx, 1	
	shl ecx, 1	
	shl edx, 1	
	mov edi, [rsp + nb010_ntia]
	add eax, edi
	add ebx, edi
	add ecx, edi
	add edx, edi

	movlps xmm6, [rsi + rax*4]
	movlps xmm7, [rsi + rcx*4]
	movhps xmm6, [rsi + rbx*4]
	movhps xmm7, [rsi + rdx*4]

	movaps xmm4, xmm6
	shufps xmm4, xmm7, 136  ;# 10001000
	shufps xmm6, xmm7, 221  ;# 11011101
	
	movd  eax, mm0		
	movd  ebx, mm1
	movd  ecx, mm2
	movd  edx, mm3

	movaps [rsp + nb010_c6], xmm4
	movaps [rsp + nb010_c12], xmm6
	
	mov rsi, [rbp + nb010_pos]       ;# base of pos[] 

	lea   eax, [eax + eax*2]     ;# replace jnr with j3 
	lea   ebx, [ebx + ebx*2]	

	mulps xmm3, xmm2
	lea   ecx, [ecx + ecx*2]     ;# replace jnr with j3 
	lea   edx, [edx + edx*2]	

	;# move four coordinates to xmm0-xmm2 	

	movlps xmm4, [rsi + rax*4]
	movlps xmm5, [rsi + rcx*4]
	movss xmm2, [rsi + rax*4 + 8]
	movss xmm6, [rsi + rcx*4 + 8]

	movhps xmm4, [rsi + rbx*4]
	movhps xmm5, [rsi + rdx*4]

	movss xmm0, [rsi + rbx*4 + 8]
	movss xmm1, [rsi + rdx*4 + 8]

	shufps xmm2, xmm0, 0
	shufps xmm6, xmm1, 0
	
	movaps xmm0, xmm4
	movaps xmm1, xmm4

	shufps xmm2, xmm6, 136  ;# 10001000
	
	shufps xmm0, xmm5, 136  ;# 10001000
	shufps xmm1, xmm5, 221  ;# 11011101		

	;# move ix-iz to xmm4-xmm6 
	movaps xmm4, [rsp + nb010_ix]
	movaps xmm5, [rsp + nb010_iy]
	movaps xmm6, [rsp + nb010_iz]

	;# calc dr 
	subps xmm4, xmm0
	subps xmm5, xmm1
	subps xmm6, xmm2

	;# store dr 
	movaps [rsp + nb010_dx], xmm4 
	movaps [rsp + nb010_dy], xmm5
	movaps [rsp + nb010_dz], xmm6
	;# square it 
	mulps xmm4,xmm4
	mulps xmm5,xmm5
	mulps xmm6,xmm6
	addps xmm4, xmm5
	addps xmm4, xmm6

	;# rsq in xmm4 
	rcpps xmm5, xmm4
	;# 1/x lookup seed in xmm5 
	movaps xmm0, [rsp + nb010_two]
	mulps xmm4, xmm5
	subps xmm0, xmm4
	mulps xmm0, xmm5	;# xmm0=rinvsq
	
	movaps xmm4, xmm0

	movaps xmm1, xmm0
	mulps  xmm1, xmm0
	mulps  xmm1, xmm0	;# xmm1=rinvsix 
	movaps xmm2, xmm1
	mulps  xmm2, xmm2	;# xmm2=rinvtwelve 

	mulps  xmm1, [rsp + nb010_c6]
	mulps  xmm2, [rsp + nb010_c12]
	movaps xmm5, xmm2
	subps  xmm5, xmm1	;# Vvdw=Vvdw12-Vvdw6 
	addps  xmm5, [rsp + nb010_Vvdwtot]
	mulps  xmm1, [rsp + nb010_six]
	mulps  xmm2, [rsp + nb010_twelve]
	subps  xmm2, xmm1
	mulps  xmm4, xmm2	;# xmm4=total fscal 

	movaps xmm0, [rsp + nb010_dx]
	movaps xmm1, [rsp + nb010_dy]
	movaps xmm2, [rsp + nb010_dz]

	movaps [rsp + nb010_Vvdwtot], xmm5

	mov    rdi, [rbp + nb010_faction]
	mulps  xmm0, xmm4
	mulps  xmm1, xmm4
	mulps  xmm2, xmm4
	;# xmm0-xmm2 contains tx-tz (partial force) 
	;# now update f_i 
	movaps xmm3, [rsp + nb010_fix]
	movaps xmm4, [rsp + nb010_fiy]
	movaps xmm5, [rsp + nb010_fiz]
	addps  xmm3, xmm0
	addps  xmm4, xmm1
	addps  xmm5, xmm2
	movaps [rsp + nb010_fix], xmm3
	movaps [rsp + nb010_fiy], xmm4
	movaps [rsp + nb010_fiz], xmm5
	;# the fj's - start by accumulating x & y forces from memory 
	movlps xmm4, [rdi + rax*4]
	movlps xmm6, [rdi + rcx*4]
	movhps xmm4, [rdi + rbx*4]
	movhps xmm6, [rdi + rdx*4]

	movaps xmm3, xmm4
	shufps xmm3, xmm6, 136  ;# 10001000
	shufps xmm4, xmm6, 221  ;# 11011101			      

	;# now xmm3-xmm5 contains fjx, fjy, fjz 
	subps  xmm3, xmm0
	subps  xmm4, xmm1
	
	;# unpack them back so we can store them - first x & y in xmm3/xmm4 

	movaps xmm6, xmm3
	unpcklps xmm6, xmm4
	unpckhps xmm3, xmm4	
	;# xmm6(l)=x & y for j1, (h) for j2 
	;# xmm3(l)=x & y for j3, (h) for j4 
	movlps [rdi + rax*4], xmm6
	movlps [rdi + rcx*4], xmm3
	
	movhps [rdi + rbx*4], xmm6
	movhps [rdi + rdx*4], xmm3

	;# and the z forces 
	movss  xmm4, [rdi + rax*4 + 8]
	movss  xmm5, [rdi + rbx*4 + 8]
	movss  xmm6, [rdi + rcx*4 + 8]
	movss  xmm7, [rdi + rdx*4 + 8]
	subss  xmm4, xmm2
	shufps xmm2, xmm2, 229  ;# 11100101
	subss  xmm5, xmm2
	shufps xmm2, xmm2, 234  ;# 11101010
	subss  xmm6, xmm2
	shufps xmm2, xmm2, 255  ;# 11111111
	subss  xmm7, xmm2
	movss  [rdi + rax*4 + 8], xmm4
	movss  [rdi + rbx*4 + 8], xmm5
	movss  [rdi + rcx*4 + 8], xmm6
	movss  [rdi + rdx*4 + 8], xmm7
	
	;# should we do one more iteration? 
	sub   dword ptr [rsp + nb010_innerk],  4
	jl    .nb010_finish_inner
	jmp   .nb010_unroll_loop
.nb010_finish_inner:
	;# check if at least two particles remain 
	add   dword ptr [rsp + nb010_innerk],  4
	mov   edx, [rsp + nb010_innerk]
	and   edx, 2
	jnz   .nb010_dopair
	jmp   .nb010_checksingle
.nb010_dopair:	
	mov   rcx, [rsp + nb010_innerjjnr]
	
	mov   eax, [rcx]	
	mov   ebx, [rcx + 4]              
	add   qword ptr [rsp + nb010_innerjjnr],  8

	mov rsi, [rbp + nb010_type]
	mov   ecx, eax
	mov   edx, ebx
	mov ecx, [rsi + rcx*4]
	mov edx, [rsi + rdx*4]	
	mov rsi, [rbp + nb010_vdwparam]
	shl ecx, 1	
	shl edx, 1	
	mov edi, [rsp + nb010_ntia]
	add ecx, edi
	add edx, edi
	movlps xmm6, [rsi + rcx*4]
	movhps xmm6, [rsi + rdx*4]
	mov rdi, [rbp + nb010_pos]	
	xorps  xmm7,xmm7
	movaps xmm4, xmm6
	shufps xmm4, xmm4, 8 ;# 00001000 	
	shufps xmm6, xmm6, 13 ;# 00001101
	movlhps xmm4, xmm7
	movlhps xmm6, xmm7
	
	movaps [rsp + nb010_c6], xmm4
	movaps [rsp + nb010_c12], xmm6	
	
	lea   eax, [eax + eax*2]
	lea   ebx, [ebx + ebx*2]
	;# move coordinates to xmm0-xmm2 
	movlps xmm1, [rdi + rax*4]
	movss xmm2, [rdi + rax*4 + 8]	
	movhps xmm1, [rdi + rbx*4]
	movss xmm0, [rdi + rbx*4 + 8]	


	movlhps xmm3, xmm7
	
	shufps xmm2, xmm0, 0
	
	movaps xmm0, xmm1

	shufps xmm2, xmm2, 136  ;# 10001000
	
	shufps xmm0, xmm0, 136  ;# 10001000
	shufps xmm1, xmm1, 221  ;# 11011101
	
	mov    rdi, [rbp + nb010_faction]
	;# move nb010_ix-iz to xmm4-xmm6 
	xorps   xmm7, xmm7
	
	movaps xmm4, [rsp + nb010_ix]
	movaps xmm5, [rsp + nb010_iy]
	movaps xmm6, [rsp + nb010_iz]

	;# calc dr 
	subps xmm4, xmm0
	subps xmm5, xmm1
	subps xmm6, xmm2

	;# store dr 
	movaps [rsp + nb010_dx], xmm4
	movaps [rsp + nb010_dy], xmm5
	movaps [rsp + nb010_dz], xmm6
	;# square it 
	mulps xmm4,xmm4
	mulps xmm5,xmm5
	mulps xmm6,xmm6
	addps xmm4, xmm5
	addps xmm4, xmm6
	;# rsq in xmm4 


	rcpps xmm5, xmm4
	;# 1/x lookup seed in xmm5 
	movaps xmm0, [rsp + nb010_two]
	mulps xmm4, xmm5
	subps xmm0, xmm4
	mulps xmm0, xmm5	;# xmm0=rinvsq 
	movaps xmm4, xmm0
	
	movaps xmm1, xmm0
	mulps  xmm1, xmm0
	mulps  xmm1, xmm0	;# xmm1=rinvsix 
	movaps xmm2, xmm1
	mulps  xmm2, xmm2	;# xmm2=rinvtwelve 

	mulps  xmm1, [rsp + nb010_c6]
	mulps  xmm2, [rsp + nb010_c12]
	movaps xmm5, xmm2
	subps  xmm5, xmm1	;# Vvdw=Vvdw12-Vvdw6 
	addps  xmm5, [rsp + nb010_Vvdwtot]
	mulps  xmm1, [rsp + nb010_six]
	mulps  xmm2, [rsp + nb010_twelve]
	subps  xmm2, xmm1
	mulps  xmm4, xmm2	;# xmm4=total fscal 

	movaps xmm0, [rsp + nb010_dx]
	movaps xmm1, [rsp + nb010_dy]
	movaps xmm2, [rsp + nb010_dz]

	movaps [rsp + nb010_Vvdwtot], xmm5

	mulps  xmm0, xmm4
	mulps  xmm1, xmm4
	mulps  xmm2, xmm4
	;# xmm0-xmm2 contains tx-tz (partial force) 
	;# now update f_i 
	movaps xmm3, [rsp + nb010_fix]
	movaps xmm4, [rsp + nb010_fiy]
	movaps xmm5, [rsp + nb010_fiz]
	addps  xmm3, xmm0
	addps  xmm4, xmm1
	addps  xmm5, xmm2
	movaps [rsp + nb010_fix], xmm3
	movaps [rsp + nb010_fiy], xmm4
	movaps [rsp + nb010_fiz], xmm5
	;# update the fj's 
	movss   xmm3, [rdi + rax*4]
	movss   xmm4, [rdi + rax*4 + 4]
	movss   xmm5, [rdi + rax*4 + 8]
	subss   xmm3, xmm0
	subss   xmm4, xmm1
	subss   xmm5, xmm2	
	movss   [rdi + rax*4], xmm3
	movss   [rdi + rax*4 + 4], xmm4
	movss   [rdi + rax*4 + 8], xmm5	

	shufps  xmm0, xmm0, 225  ;# 11100001
	shufps  xmm1, xmm1, 225  ;# 11100001
	shufps  xmm2, xmm2, 225  ;# 11100001

	movss   xmm3, [rdi + rbx*4]
	movss   xmm4, [rdi + rbx*4 + 4]
	movss   xmm5, [rdi + rbx*4 + 8]
	subss   xmm3, xmm0
	subss   xmm4, xmm1
	subss   xmm5, xmm2	
	movss   [rdi + rbx*4], xmm3
	movss   [rdi + rbx*4 + 4], xmm4
	movss   [rdi + rbx*4 + 8], xmm5	

.nb010_checksingle:				
	mov   edx, [rsp + nb010_innerk]
	and   edx, 1
	jnz    .nb010_dosingle
	jmp    .nb010_updateouterdata
.nb010_dosingle:
	mov rdi, [rbp + nb010_pos]
	mov   rcx, [rsp + nb010_innerjjnr]
	mov   eax, [rcx]		

	mov rsi, [rbp + nb010_type]
	mov ecx, eax
	mov ecx, [rsi + rcx*4]	
	mov rsi, [rbp + nb010_vdwparam]
	shl ecx, 1
	add ecx, [rsp + nb010_ntia]
	xorps  xmm6, xmm6
	movlps xmm6, [rsi + rcx*4]
	movaps xmm4, xmm6
	shufps xmm4, xmm4, 252  ;# 11111100
	shufps xmm6, xmm6, 253  ;# 11111101	
	
	movaps [rsp + nb010_c6], xmm4
	movaps [rsp + nb010_c12], xmm6	
	
	lea   eax, [eax + eax*2]
	
	;# move coordinates to xmm0-xmm2 
	movss xmm0, [rdi + rax*4]	
	movss xmm1, [rdi + rax*4 + 4]	
	movss xmm2, [rdi + rax*4 + 8]	
	
	xorps   xmm7, xmm7
	
	movaps xmm4, [rsp + nb010_ix]
	movaps xmm5, [rsp + nb010_iy]
	movaps xmm6, [rsp + nb010_iz]

	;# calc dr 
	subps xmm4, xmm0
	subps xmm5, xmm1
	subps xmm6, xmm2

	;# store dr 
	movaps [rsp + nb010_dx], xmm4
	movaps [rsp + nb010_dy], xmm5
	movaps [rsp + nb010_dz], xmm6
	;# square it 
	mulps xmm4,xmm4
	mulps xmm5,xmm5
	mulps xmm6,xmm6
	addps xmm4, xmm5
	addps xmm4, xmm6
	;# rsq in xmm4 

	rcpps xmm5, xmm4
	;# 1/x lookup seed in xmm5 
	movaps xmm0, [rsp + nb010_two]
	mulps xmm4, xmm5
	subps xmm0, xmm4
	mulps xmm0, xmm5	;# xmm0=rinvsq 
	movaps xmm4, xmm0
	
	movaps xmm1, xmm0
	mulps  xmm1, xmm0
	mulps  xmm1, xmm0	;# xmm1=rinvsix 
	movaps xmm2, xmm1
	mulps  xmm2, xmm2	;# xmm2=rinvtwelve 

	mulps  xmm1, [rsp + nb010_c6]
	mulps  xmm2, [rsp + nb010_c12]
	movaps xmm5, xmm2
	subps  xmm5, xmm1	;# Vvdw=Vvdw12-Vvdw6 
	addss  xmm5, [rsp + nb010_Vvdwtot]
	mulps  xmm1, [rsp + nb010_six]
	mulps  xmm2, [rsp + nb010_twelve]
	subps  xmm2, xmm1
	mulps  xmm4, xmm2	;# xmm4=total fscal 
	
	mov    rdi, [rbp + nb010_faction]

	movaps xmm0, [rsp + nb010_dx]
	movaps xmm1, [rsp + nb010_dy]
	movaps xmm2, [rsp + nb010_dz]

	movss [rsp + nb010_Vvdwtot], xmm5

	mulps  xmm0, xmm4
	mulps  xmm1, xmm4
	mulps  xmm2, xmm4
	;# xmm0-xmm2 contains tx-tz (partial force) 
	;# now update f_i 
	movaps xmm3, [rsp + nb010_fix]
	movaps xmm4, [rsp + nb010_fiy]
	movaps xmm5, [rsp + nb010_fiz]
	addss  xmm3, xmm0
	addss  xmm4, xmm1
	addss  xmm5, xmm2
	movaps [rsp + nb010_fix], xmm3
	movaps [rsp + nb010_fiy], xmm4
	movaps [rsp + nb010_fiz], xmm5
	;# update fj 
	
	movss   xmm3, [rdi + rax*4]
	movss   xmm4, [rdi + rax*4 + 4]
	movss   xmm5, [rdi + rax*4 + 8]
	subss   xmm3, xmm0
	subss   xmm4, xmm1
	subss   xmm5, xmm2	
	movss   [rdi + rax*4], xmm3
	movss   [rdi + rax*4 + 4], xmm4
	movss   [rdi + rax*4 + 8], xmm5
	
.nb010_updateouterdata:
	mov   ecx, [rsp + nb010_ii3]
	mov   rdi, [rbp + nb010_faction]
	mov   rsi, [rbp + nb010_fshift]
	mov   edx, [rsp + nb010_is3]

	;# accumulate i forces in xmm0, xmm1, xmm2 
	movaps xmm0, [rsp + nb010_fix]
	movaps xmm1, [rsp + nb010_fiy]
	movaps xmm2, [rsp + nb010_fiz]

	movhlps xmm3, xmm0
	movhlps xmm4, xmm1
	movhlps xmm5, xmm2
	addps  xmm0, xmm3
	addps  xmm1, xmm4
	addps  xmm2, xmm5 ;# sum is in 1/2 in xmm0-xmm2 

	movaps xmm3, xmm0	
	movaps xmm4, xmm1	
	movaps xmm5, xmm2	

	shufps xmm3, xmm3, 1
	shufps xmm4, xmm4, 1
	shufps xmm5, xmm5, 1
	addss  xmm0, xmm3
	addss  xmm1, xmm4
	addss  xmm2, xmm5	;# xmm0-xmm2 has single force in pos0 

	;# increment i force 
	movss  xmm3, [rdi + rcx*4]
	movss  xmm4, [rdi + rcx*4 + 4]
	movss  xmm5, [rdi + rcx*4 + 8]
	addss  xmm3, xmm0
	addss  xmm4, xmm1
	addss  xmm5, xmm2
	movss  [rdi + rcx*4],     xmm3
	movss  [rdi + rcx*4 + 4], xmm4
	movss  [rdi + rcx*4 + 8], xmm5

	;# increment fshift force  
	movss  xmm3, [rsi + rdx*4]
	movss  xmm4, [rsi + rdx*4 + 4]
	movss  xmm5, [rsi + rdx*4 + 8]
	addss  xmm3, xmm0
	addss  xmm4, xmm1
	addss  xmm5, xmm2
	movss  [rsi + rdx*4],     xmm3
	movss  [rsi + rdx*4 + 4], xmm4
	movss  [rsi + rdx*4 + 8], xmm5

	;# get n from stack
	mov esi, [rsp + nb010_n]
        ;# get group index for i particle 
        mov   rdx, [rbp + nb010_gid]      	;# base of gid[]
        mov   edx, [rdx + rsi*4]		;# ggid=gid[n]

         ;# accumulate total lj energy and update it 
        movaps xmm7, [rsp + nb010_Vvdwtot]
        ;# accumulate 
        movhlps xmm6, xmm7
        addps  xmm7, xmm6       ;# pos 0-1 in xmm7 have the sum now 
        movaps xmm6, xmm7
        shufps xmm6, xmm6, 1
        addss  xmm7, xmm6

        ;# add earlier value from mem 
        mov   rax, [rbp + nb010_Vvdw]
        addss xmm7, [rax + rdx*4] 
        ;# move back to mem 
        movss [rax + rdx*4], xmm7 

        ;# finish if last 
        mov ecx, [rsp + nb010_nn1]
	;# esi already loaded with n
	inc esi
        sub ecx, esi
        jecxz .nb010_outerend

        ;# not last, iterate outer loop once more!  
        mov [rsp + nb010_n], esi
        jmp .nb010_outer
.nb010_outerend:
        ;# check if more outer neighborlists remain
        mov   ecx, [rsp + nb010_nri]
	;# esi already loaded with n above
        sub   ecx, esi
        jecxz .nb010_end
        ;# non-zero, do one more workunit
        jmp   .nb010_threadloop
.nb010_end:
	
	femms

	mov eax, [rsp + nb010_nouter]
	mov ebx, [rsp + nb010_ninner]
	mov rcx, [rbp + nb010_outeriter]
	mov rdx, [rbp + nb010_inneriter]
	mov [rcx], eax
	mov [rdx], ebx

	add rsp, 392
	
	pop rbx
	pop	rbp
	ret







	
.globl nb_kernel010nf_x86_64_sse
.globl _nb_kernel010nf_x86_64_sse
nb_kernel010nf_x86_64_sse:	
_nb_kernel010nf_x86_64_sse:	
;#	Room for return address and rbp (16 bytes)
.equiv          nb010nf_fshift,         16
.equiv          nb010nf_gid,            24
.equiv          nb010nf_pos,            32
.equiv          nb010nf_faction,        40
.equiv          nb010nf_charge,         48
.equiv          nb010nf_p_facel,        56
.equiv          nb010nf_argkrf,         64
.equiv          nb010nf_argcrf,         72
.equiv          nb010nf_Vc,             80
.equiv          nb010nf_type,           88
.equiv          nb010nf_p_ntype,        96
.equiv          nb010nf_vdwparam,       104
.equiv          nb010nf_Vvdw,           112
.equiv          nb010nf_p_tabscale,     120
.equiv          nb010nf_VFtab,          128
.equiv          nb010nf_invsqrta,       136
.equiv          nb010nf_dvda,           144
.equiv          nb010nf_p_gbtabscale,   152
.equiv          nb010nf_GBtab,          160
.equiv          nb010nf_p_nthreads,     168
.equiv          nb010nf_count,          176
.equiv          nb010nf_mtx,            184
.equiv          nb010nf_outeriter,      192
.equiv          nb010nf_inneriter,      200
.equiv          nb010nf_work,           208
        ;# The mutex (last arg) is not used in assembly.
	;# stack offsets for local variables  
	;# bottom of stack is cache-aligned for sse use 
.equiv          nb010nf_ix,             0
.equiv          nb010nf_iy,             16
.equiv          nb010nf_iz,             32
.equiv          nb010nf_two,            48
.equiv          nb010nf_c6,             64
.equiv          nb010nf_c12,            80
.equiv          nb010nf_Vvdwtot,        96
.equiv          nb010nf_half,           112
.equiv          nb010nf_three,          128
.equiv          nb010nf_nri,            144
.equiv          nb010nf_iinr,           152
.equiv          nb010nf_jindex,         160
.equiv          nb010nf_jjnr,           168
.equiv          nb010nf_shift,          176
.equiv          nb010nf_shiftvec,       184
.equiv          nb010nf_innerjjnr,      192
.equiv          nb010nf_facel,          200
.equiv          nb010nf_ntia,           208
.equiv          nb010nf_innerk,         216
.equiv          nb010nf_is3,            220
.equiv          nb010nf_ii3,            224
.equiv          nb010nf_n,              228
.equiv          nb010nf_nn1,            232
.equiv          nb010nf_ntype,          236
.equiv          nb010nf_nouter,         240
.equiv          nb010nf_ninner,         244

	push rbp
	mov  rbp, rsp
	push rbx

        sub rsp, 264		; # local variable stack space (n*16+8)                                                         
	femms

	;# zero 32-bit iteration counters
	mov eax, 0
	mov [rsp + nb010nf_nouter], eax
	mov [rsp + nb010nf_ninner], eax


	mov edi, [rdi]
	mov [rsp + nb010nf_nri], edi
	mov [rsp + nb010nf_iinr], rsi
	mov [rsp + nb010nf_jindex], rdx
	mov [rsp + nb010nf_jjnr], rcx
	mov [rsp + nb010nf_shift], r8
	mov [rsp + nb010nf_shiftvec], r9
	mov rdi, [rbp + nb010nf_p_ntype]
	mov edi, [rdi]
	mov [rsp + nb010nf_ntype], edi

	;# create constant floating-point factors on stack
	mov eax, 0x40000000     ;# 2.0 in IEEE (hex)
	mov [rsp + nb010nf_two], eax
	movss xmm1, [rsp + nb010nf_two]
	shufps xmm1, xmm1, 0    ;# splat to all elements
	movaps [rsp + nb010nf_two], xmm1

.nb010nf_threadloop:
        mov   rsi, [rbp + nb010nf_count]        ;# pointer to sync counter
        mov   eax, [rsi]
.nb010nf_spinlock:
        mov   ebx, eax                          ;# ebx=*count=nn0
        add   ebx, 1                           	;# ebx=nn1=nn0+10
        lock cmpxchg [rsi], ebx                 ;# write nn1 to *counter,
                                                ;# if it hasnt changed.
                                                ;# or reread *counter to eax.
        pause                                   ;# -> better p4 performance
        jnz .nb010nf_spinlock

        ;# if(nn1>nri) nn1=nri
        mov ecx, [rsp + nb010nf_nri]
        mov edx, ecx
        sub ecx, ebx
        cmovle ebx, edx                         ;# if(nn1>nri) nn1=nri
        ;# Cleared the spinlock if we got here.
        ;# eax contains nn0, ebx contains nn1.
        mov [rsp + nb010nf_n], eax
        mov [rsp + nb010nf_nn1], ebx
        sub ebx, eax                            ;# calc number of outer lists
	mov esi, eax				;# copy n to esi
        jg  .nb010nf_outerstart
        jmp .nb010nf_end

.nb010nf_outerstart:
	;# ebx contains number of outer iterations
	add ebx, [rsp + nb010nf_nouter]
	mov [rsp + nb010nf_nouter], ebx

.nb010nf_outer:
        mov   rax, [rsp + nb010nf_shift]      	;# rax = base of shift[] 
        mov   ebx, [rax +rsi*4]                	;# ebx=shift[n] 
        
        lea   ebx, [ebx + ebx*2]    		;# ebx=3*is 
        mov   [rsp + nb010nf_is3],ebx           ;# store is3 

        mov   rax, [rsp + nb010nf_shiftvec]   	;# rax = base of shiftvec[] 

        movss xmm0, [rax + rbx*4]
        movss xmm1, [rax + rbx*4 + 4]
        movss xmm2, [rax + rbx*4 + 8] 

        mov   rcx, [rsp + nb010nf_iinr]       	;# rcx = base of iinr[] 
        mov   ebx, [rcx + rsi*4]            	;# ebx =ii 

        mov   rdx, [rbp + nb010nf_type] 
        mov   edx, [rdx + rbx*4]
        imul  edx, [rsp + nb010nf_ntype]
        shl   edx, 1
        mov   [rsp + nb010nf_ntia], edx

        lea   ebx, [ebx + ebx*2]        ;# ebx = 3*ii=ii3 
        mov   rax, [rbp + nb010nf_pos]    ;# rax = base of pos[]  

        addss xmm0, [rax + rbx*4]
        addss xmm1, [rax + rbx*4 + 4]
        addss xmm2, [rax + rbx*4 + 8]

        shufps xmm0, xmm0, 0
        shufps xmm1, xmm1, 0
        shufps xmm2, xmm2, 0

	movaps [rsp + nb010nf_ix], xmm0
	movaps [rsp + nb010nf_iy], xmm1
	movaps [rsp + nb010nf_iz], xmm2

	mov   [rsp + nb010nf_ii3], ebx
	
	;# clear Vvdwtot and i forces 
	xorps xmm4, xmm4
	movaps [rsp + nb010nf_Vvdwtot], xmm4
	
        mov   rax, [rsp + nb010nf_jindex]
        mov   ecx, [rax + rsi*4]             ;# jindex[n] 
        mov   edx, [rax + rsi*4 + 4]         ;# jindex[n+1] 
        sub   edx, ecx               ;# number of innerloop atoms 

	mov   rsi, [rbp + nb010nf_pos]
	mov   rax, [rsp + nb010nf_jjnr]
	shl   ecx, 2
	add   rax, rcx
	mov   [rsp + nb010nf_innerjjnr], rax 	;# pointer to jjnr[nj0] 
	mov   ecx, edx
	sub   edx,  4
	add   ecx, [rsp + nb010nf_ninner]
	mov   [rsp + nb010nf_ninner], ecx
	add   edx, 0
	mov   [rsp + nb010nf_innerk], edx    	;# number of innerloop atoms 
	
	jge   .nb010nf_unroll_loop
	jmp   .nb010nf_finish_inner
.nb010nf_unroll_loop:
	;# quad-unroll innerloop here 
	mov   rdx, [rsp + nb010nf_innerjjnr]     ;# pointer to jjnr[k] 
	mov   eax, [rdx]	
	mov   ebx, [rdx + 4]              
	mov   ecx, [rdx + 8]            
	mov   edx, [rdx + 12]         ;# eax-edx=jnr1-4 
	;# advance pointer (unrolled 4) 
	add   qword ptr [rsp + nb010nf_innerjjnr],  16 

	movd  mm0, eax		;# use mmx registers as temp storage 
	movd  mm1, ebx
	movd  mm2, ecx
	movd  mm3, edx
	
	mov rsi, [rbp + nb010nf_type]
	mov eax, [rsi + rax*4]
	mov ebx, [rsi + rbx*4]
	mov ecx, [rsi + rcx*4]
	mov edx, [rsi + rdx*4]
	mov rsi, [rbp + nb010nf_vdwparam]
	shl eax, 1	
	shl ebx, 1	
	shl ecx, 1	
	shl edx, 1	
	mov edi, [rsp + nb010nf_ntia]
	add eax, edi
	add ebx, edi
	add ecx, edi
	add edx, edi

	movlps xmm6, [rsi + rax*4]
	movlps xmm7, [rsi + rcx*4]
	movhps xmm6, [rsi + rbx*4]
	movhps xmm7, [rsi + rdx*4]

	movaps xmm4, xmm6
	shufps xmm4, xmm7, 136  ;# 10001000
	shufps xmm6, xmm7, 221  ;# 11011101
	
	movd  eax, mm0		
	movd  ebx, mm1
	movd  ecx, mm2
	movd  edx, mm3

	movaps [rsp + nb010nf_c6], xmm4
	movaps [rsp + nb010nf_c12], xmm6
	
	mov rsi, [rbp + nb010nf_pos]       ;# base of pos[] 

	lea   eax, [eax + eax*2]     ;# replace jnr with j3 
	lea   ebx, [ebx + ebx*2]	

	mulps xmm3, xmm2
	lea   ecx, [ecx + ecx*2]     ;# replace jnr with j3 
	lea   edx, [edx + edx*2]	

	;# move four coordinates to xmm0-xmm2 	

	movlps xmm4, [rsi + rax*4]
	movlps xmm5, [rsi + rcx*4]
	movss xmm2, [rsi + rax*4 + 8]
	movss xmm6, [rsi + rcx*4 + 8]

	movhps xmm4, [rsi + rbx*4]
	movhps xmm5, [rsi + rdx*4]

	movss xmm0, [rsi + rbx*4 + 8]
	movss xmm1, [rsi + rdx*4 + 8]

	shufps xmm2, xmm0, 0
	shufps xmm6, xmm1, 0
	
	movaps xmm0, xmm4
	movaps xmm1, xmm4

	shufps xmm2, xmm6, 136  ;# 10001000
	
	shufps xmm0, xmm5, 136  ;# 10001000
	shufps xmm1, xmm5, 221  ;# 11011101		

	;# move ix-iz to xmm4-xmm6 
	movaps xmm4, [rsp + nb010nf_ix]
	movaps xmm5, [rsp + nb010nf_iy]
	movaps xmm6, [rsp + nb010nf_iz]

	;# calc dr 
	subps xmm4, xmm0
	subps xmm5, xmm1
	subps xmm6, xmm2

	;# square it 
	mulps xmm4,xmm4
	mulps xmm5,xmm5
	mulps xmm6,xmm6
	addps xmm4, xmm5
	addps xmm4, xmm6

	;# rsq in xmm4 
	rcpps xmm5, xmm4
	;# 1/x lookup seed in xmm5 
	movaps xmm0, [rsp + nb010nf_two]
	mulps xmm4, xmm5
	subps xmm0, xmm4
	mulps xmm0, xmm5	;# xmm0=rinvsq 
	movaps xmm4, xmm0

	movaps xmm1, xmm0
	mulps  xmm1, xmm0
	mulps  xmm1, xmm0	;# xmm1=rinvsix 
	movaps xmm2, xmm1
	mulps  xmm2, xmm2	;# xmm2=rinvtwelve 

	mulps  xmm1, [rsp + nb010nf_c6]
	mulps  xmm2, [rsp + nb010nf_c12]
	movaps xmm5, xmm2
	subps  xmm5, xmm1	;# Vvdw=Vvdw12-Vvdw6 
	addps  xmm5, [rsp + nb010nf_Vvdwtot]
	movaps [rsp + nb010nf_Vvdwtot], xmm5

	;# should we do one more iteration? 
	sub   dword ptr [rsp + nb010nf_innerk],  4
	jl    .nb010nf_finish_inner
	jmp   .nb010nf_unroll_loop
.nb010nf_finish_inner:
	;# check if at least two particles remain 
	add   dword ptr [rsp + nb010nf_innerk],  4
	mov   edx, [rsp + nb010nf_innerk]
	and   edx, 2
	jnz   .nb010nf_dopair
	jmp   .nb010nf_checksingle
.nb010nf_dopair:	
	mov   rcx, [rsp + nb010nf_innerjjnr]
	
	mov   eax, [rcx]	
	mov   ebx, [rcx + 4]              
	add   qword ptr [rsp + nb010nf_innerjjnr],  8

	mov rsi, [rbp + nb010nf_type]
	mov   ecx, eax
	mov   edx, ebx
	mov ecx, [rsi + rcx*4]
	mov edx, [rsi + rdx*4]	
	mov rsi, [rbp + nb010nf_vdwparam]
	shl ecx, 1	
	shl edx, 1	
	mov edi, [rsp + nb010nf_ntia]
	add ecx, edi
	add edx, edi
	movlps xmm6, [rsi + rcx*4]
	movhps xmm6, [rsi + rdx*4]
	mov rdi, [rbp + nb010nf_pos]	
	xorps  xmm7,xmm7
	movaps xmm4, xmm6
	shufps xmm4, xmm4, 8 ;# 00001000 	
	shufps xmm6, xmm6, 13 ;# 00001101
	movlhps xmm4, xmm7
	movlhps xmm6, xmm7
	
	movaps [rsp + nb010nf_c6], xmm4
	movaps [rsp + nb010nf_c12], xmm6	
	
	lea   eax, [eax + eax*2]
	lea   ebx, [ebx + ebx*2]
	;# move coordinates to xmm0-xmm2 
	movlps xmm1, [rdi + rax*4]
	movss xmm2, [rdi + rax*4 + 8]	
	movhps xmm1, [rdi + rbx*4]
	movss xmm0, [rdi + rbx*4 + 8]	

	movlhps xmm3, xmm7
	
	shufps xmm2, xmm0, 0
	
	movaps xmm0, xmm1

	shufps xmm2, xmm2, 136  ;# 10001000
	
	shufps xmm0, xmm0, 136  ;# 10001000
	shufps xmm1, xmm1, 221  ;# 11011101
	
	;# move nb010nf_ix-iz to xmm4-xmm6 
	xorps   xmm7, xmm7
	
	movaps xmm4, [rsp + nb010nf_ix]
	movaps xmm5, [rsp + nb010nf_iy]
	movaps xmm6, [rsp + nb010nf_iz]

	;# calc dr 
	subps xmm4, xmm0
	subps xmm5, xmm1
	subps xmm6, xmm2

	;# square it 
	mulps xmm4,xmm4
	mulps xmm5,xmm5
	mulps xmm6,xmm6
	addps xmm4, xmm5
	addps xmm4, xmm6
	;# rsq in xmm4 


	rcpps xmm5, xmm4
	;# 1/x lookup seed in xmm5 
	movaps xmm0, [rsp + nb010nf_two]
	mulps xmm4, xmm5
	subps xmm0, xmm4
	mulps xmm0, xmm5	;# xmm0=rinvsq 
	movaps xmm4, xmm0
	
	movaps xmm1, xmm0
	mulps  xmm1, xmm0
	mulps  xmm1, xmm0	;# xmm1=rinvsix 
	movaps xmm2, xmm1
	mulps  xmm2, xmm2	;# xmm2=rinvtwelve 

	mulps  xmm1, [rsp + nb010nf_c6]
	mulps  xmm2, [rsp + nb010nf_c12]
	movaps xmm5, xmm2
	subps  xmm5, xmm1	;# Vvdw=Vvdw12-Vvdw6 
	addps  xmm5, [rsp + nb010nf_Vvdwtot]
	movaps [rsp + nb010nf_Vvdwtot], xmm5

.nb010nf_checksingle:				
	mov   edx, [rsp + nb010nf_innerk]
	and   edx, 1
	jnz    .nb010nf_dosingle
	jmp    .nb010nf_updateouterdata
.nb010nf_dosingle:
	mov rdi, [rbp + nb010nf_pos]
	mov   rcx, [rsp + nb010nf_innerjjnr]
	mov   eax, [rcx]		

	mov rsi, [rbp + nb010nf_type]
	mov ecx, eax
	mov ecx, [rsi + rcx*4]	
	mov rsi, [rbp + nb010nf_vdwparam]
	shl ecx, 1
	add ecx, [rsp + nb010nf_ntia]
	xorps  xmm6, xmm6
	movlps xmm6, [rsi + rcx*4]
	movaps xmm4, xmm6
	shufps xmm4, xmm4, 252  ;# 11111100
	shufps xmm6, xmm6, 253  ;# 11111101	
	
	movaps [rsp + nb010nf_c6], xmm4
	movaps [rsp + nb010nf_c12], xmm6	
	
	lea   eax, [eax + eax*2]
	
	;# move coordinates to xmm0-xmm2 
	movss xmm0, [rdi + rax*4]	
	movss xmm1, [rdi + rax*4 + 4]	
	movss xmm2, [rdi + rax*4 + 8]	
	
	xorps   xmm7, xmm7
	
	movaps xmm4, [rsp + nb010nf_ix]
	movaps xmm5, [rsp + nb010nf_iy]
	movaps xmm6, [rsp + nb010nf_iz]

	;# calc dr 
	subps xmm4, xmm0
	subps xmm5, xmm1
	subps xmm6, xmm2

	;# square it 
	mulps xmm4,xmm4
	mulps xmm5,xmm5
	mulps xmm6,xmm6
	addps xmm4, xmm5
	addps xmm4, xmm6
	;# rsq in xmm4 

	rcpps xmm5, xmm4
	;# 1/x lookup seed in xmm5 
	movaps xmm0, [rsp + nb010nf_two]
	mulps xmm4, xmm5
	subps xmm0, xmm4
	mulps xmm0, xmm5	;# xmm0=rinvsq 
	movaps xmm4, xmm0
	
	movaps xmm1, xmm0
	mulps  xmm1, xmm0
	mulps  xmm1, xmm0	;# xmm1=rinvsix 
	movaps xmm2, xmm1
	mulps  xmm2, xmm2	;# xmm2=rinvtwelve 

	mulps  xmm1, [rsp + nb010nf_c6]
	mulps  xmm2, [rsp + nb010nf_c12]
	movaps xmm5, xmm2
	subps  xmm5, xmm1	;# Vvdw=Vvdw12-Vvdw6 
	addss  xmm5, [rsp + nb010nf_Vvdwtot]
	movss [rsp + nb010nf_Vvdwtot], xmm5
	
.nb010nf_updateouterdata:
	;# get n from stack
	mov esi, [rsp + nb010nf_n]
        ;# get group index for i particle 
        mov   rdx, [rbp + nb010nf_gid]      	;# base of gid[]
        mov   edx, [rdx + rsi*4]		;# ggid=gid[n]

        ;# accumulate total lj energy and update it 
        movaps xmm7, [rsp + nb010nf_Vvdwtot]
        ;# accumulate 
        movhlps xmm6, xmm7
        addps  xmm7, xmm6       ;# pos 0-1 in xmm7 have the sum now 
        movaps xmm6, xmm7
        shufps xmm6, xmm6, 1
        addss  xmm7, xmm6

        ;# add earlier value from mem 
        mov   rax, [rbp + nb010nf_Vvdw]
        addss xmm7, [rax + rdx*4] 
        ;# move back to mem 
        movss [rax + rdx*4], xmm7 

        ;# finish if last 
        mov ecx, [rsp + nb010nf_nn1]
	;# esi already loaded with n
	inc esi
        sub ecx, esi
        jecxz .nb010nf_outerend

        ;# not last, iterate outer loop once more!  
        mov [rsp + nb010nf_n], esi
        jmp .nb010nf_outer
.nb010nf_outerend:
        ;# check if more outer neighborlists remain
        mov   ecx, [rsp + nb010nf_nri]
	;# esi already loaded with n above
        sub   ecx, esi
        jecxz .nb010nf_end
        ;# non-zero, do one more workunit
        jmp   .nb010nf_threadloop
.nb010nf_end:

	mov eax, [rsp + nb010nf_nouter]
	mov ebx, [rsp + nb010nf_ninner]
	mov rcx, [rbp + nb010nf_outeriter]
	mov rdx, [rbp + nb010nf_inneriter]
	mov [rcx], eax
	mov [rdx], ebx

	add rsp, 264
	femms
	
	pop rbx
	pop	rbp
	ret