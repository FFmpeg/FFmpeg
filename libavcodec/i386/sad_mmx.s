;  MMX/SSE optimized routines for SAD of 16*16 macroblocks
;	Copyright (C) Juan J. Sierralta P. <juanjo@atmlab.utfsm.cl>
;
;  dist1_* Original Copyright (C) 2000 Chris Atenasio <chris@crud.net>
;  Enhancements and rest Copyright (C) 2000 Andrew Stevens <as@comlab.ox.ac.uk>

;
;  This program is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License
;  as published by the Free Software Foundation; either version 2
;  of the License, or (at your option) any later version.
;
;  This program is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this program; if not, write to the Free Software
;  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
;

global pix_abs16x16_mmx

; int  pix_abs16x16_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = distance accumulators (4 words) 
; mm2 = temp 
; mm3 = temp
; mm4 = temp
; mm5 = temp 
; mm6 = 0
; mm7 = temp


align 32
pix_abs16x16_mmx:
	push				ebp					; save frame pointer
	mov				ebp, esp

	push				ebx					; Saves registers (called saves convention in
	push				ecx					; x86 GCC it seems)
	push				edx					; 
	push				esi
	push				edi
		
	pxor				mm0, mm0				; zero acculumators
	pxor				mm1, mm1
	pxor				mm6, mm6
	mov				esi, [ebp+8]		; get pix1
	mov				edi, [ebp+12]		; get pix2
	mov				edx, [ebp+16]		; get lx
	mov				ecx, [ebp+20]		; get rowsleft
	jmp				.nextrow
align 32

.nextrow:
	; First 8 bytes of the row
	
	movq				mm4, [edi]	; load first 8 bytes of pix2 row 
	movq				mm5, [esi]	; load first 8 bytes of pix1 row
	movq				mm3, mm4		; mm4 := abs(mm4-mm5)
	movq				mm2,[esi+8]	; load last 8 bytes of pix1 row	
	psubusb			mm4, mm5
	movq				mm7,[edi+8]	; load last 8 bytes of pix2 row 		
	psubusb			mm5, mm3
	por				mm4, mm5
	
	; Last 8 bytes of the row	

	movq				mm3, mm7		; mm7 := abs(mm7-mm2)
	psubusb			mm7, mm2
	psubusb			mm2, mm3
	por				mm7, mm2
	
	; Now mm4 and mm7 have 16 absdiffs to add
	
	; First 8 bytes of the row2
	
	
	add				edi, edx
	movq				mm2, [edi]	; load first 8 bytes of pix2 row 
	add				esi, edx
	movq				mm5, [esi]	; load first 8 bytes of pix1 row
	
	
	
	movq				mm3, mm2		; mm2 := abs(mm2-mm5)
	psubusb			mm2, mm5
	movq				mm6,[esi+8]	; load last 8 bytes of pix1 row
	psubusb			mm5, mm3
	por				mm2, mm5
		
	; Last 8 bytes of the row2
	
	movq				mm5,[edi+8]	; load last 8 bytes of pix2 row 
	
	
	movq				mm3, mm5		; mm5 := abs(mm5-mm6)
	psubusb			mm5, mm6
	psubusb			mm6, mm3
	por				mm5, mm6

	; Now mm2, mm4, mm5, mm7 have 32 absdiffs
	
	movq				mm3, mm7
	
	pxor				mm6, mm6		; Zero mm6
		
	punpcklbw		mm3, mm6		; Unpack to words and add
	punpckhbw		mm7, mm6
	paddusw			mm7, mm3
	
	movq				mm3, mm5
	
	punpcklbw		mm3, mm6		; Unpack to words and add
	punpckhbw		mm5, mm6
	paddusw			mm5, mm3
		
	paddusw			mm0, mm7		; Add to the acumulator (mm0)
	paddusw			mm1, mm5		; Add to the acumulator (mm1)
		
	movq				mm3, mm4
	
	punpcklbw		mm3, mm6		; Unpack to words and add
	punpckhbw		mm4, mm6
	movq				mm5, mm2
	paddusw			mm4, mm3

	
	
	punpcklbw		mm5, mm6		; Unpack to words and add
	punpckhbw		mm2, mm6
	paddusw			mm2, mm5

	; Loop termination
	
	add				esi, edx		; update pointers to next row
	paddusw			mm0, mm4		; Add to the acumulator (mm0)
	add				edi, edx		
	sub				ecx,2
	paddusw			mm1, mm2		; Add to the acumulator (mm1)
	test				ecx, ecx		; check rowsleft
	jnz				near .nextrow
	
	paddusw			mm0, mm1
	movq				mm2, mm0		; Copy mm0 to mm2
	psrlq				mm2, 32
	paddusw			mm0, mm2		; Add 
	movq				mm3, mm0
	psrlq				mm3, 16
	paddusw			mm0, mm3
	movd				eax, mm0		; Store return value
	and				eax, 0xffff

	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp							; restore stack pointer

	;emms								; clear mmx registers
	ret								; return

global pix_abs16x16_sse

; int  pix_abs16x16_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = distance accumulators (4 words) 
; mm2 = temp 
; mm3 = temp
; mm4 = temp
; mm5 = temp 
; mm6 = temp
; mm7 = temp


align 32
pix_abs16x16_sse:
	push				ebp					; save frame pointer
	mov				ebp, esp

	push				ebx					; Saves registers (called saves convention in
	push				ecx					; x86 GCC it seems)
	push				edx					; 
	push				esi
	push				edi
		
	pxor				mm0, mm0				; zero acculumators
	pxor				mm1, mm1
	mov				esi, [ebp+8]		; get pix1
	mov				edi, [ebp+12]		; get pix2
	mov				edx, [ebp+16]		; get lx
	mov				ecx, [ebp+20]		; get rowsleft
	jmp				.next4row
align 32

.next4row:
	; First row
	
	movq				mm4, [edi]		; load first 8 bytes of pix2 row 
	movq				mm5, [edi+8]	; load last 8 bytes of pix2 row
	psadbw			mm4, [esi]		; SAD of first 8 bytes
	psadbw			mm5, [esi+8]	; SAD of last 8 bytes
	paddw				mm0, mm4			; Add to acumulators
	paddw				mm1, mm5
		
	; Second row	

	add				edi, edx;
	add				esi, edx;
	
	movq				mm6, [edi]		; load first 8 bytes of pix2 row 
	movq				mm7, [edi+8]	; load last 8 bytes of pix2 row
	psadbw			mm6, [esi]		; SAD of first 8 bytes
	psadbw			mm7, [esi+8]	; SAD of last 8 bytes
	paddw				mm0, mm6			; Add to acumulators
	paddw				mm1, mm7
		
	; Third row
	
	add				edi, edx;
	add				esi, edx;
	
	movq				mm4, [edi]		; load first 8 bytes of pix2 row 
	movq				mm5, [edi+8]	; load last 8 bytes of pix2 row
	psadbw			mm4, [esi]		; SAD of first 8 bytes
	psadbw			mm5, [esi+8]	; SAD of last 8 bytes
	paddw				mm0, mm4			; Add to acumulators
	paddw				mm1, mm5
		
	; Fourth row	

	add				edi, edx;
	add				esi, edx;
	
	movq				mm6, [edi]		; load first 8 bytes of pix2 row 
	movq				mm7, [edi+8]	; load last 8 bytes of pix2 row
	psadbw			mm6, [esi]		; SAD of first 8 bytes
	psadbw			mm7, [esi+8]	; SAD of last 8 bytes
	paddw				mm0, mm6			; Add to acumulators
	paddw				mm1, mm7
	
	; Loop termination
	
	add				esi, edx		; update pointers to next row
	add				edi, edx		
	sub				ecx,4
	test				ecx, ecx		; check rowsleft
	jnz				near .next4row
	
	paddd				mm0, mm1		; Sum acumulators
	movd				eax, mm0		; Store return value

	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp							; restore stack pointer

	;emms								; clear mmx registers
	ret								; return
		
global pix_abs16x16_x2_mmx

; int  pix_abs16x16_x2_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = distance accumulators (4 words) 
; mm2 = temp 
; mm3 = temp
; mm4 = temp
; mm5 = temp 
; mm6 = 0
; mm7 = temp


align 32
pix_abs16x16_x2_mmx:
	push				ebp					; save frame pointer
	mov				ebp, esp

	push				ebx					; Saves registers (called saves convention in
	push				ecx					; x86 GCC it seems)
	push				edx					; 
	push				esi
	push				edi
		
	pxor				mm0, mm0				; zero acculumators
	pxor				mm1, mm1
	pxor				mm6, mm6
	mov				esi, [ebp+8]		; get pix1
	mov				edi, [ebp+12]		; get pix2
	mov				edx, [ebp+16]		; get lx
	mov				ecx, [ebp+20]		; get rowsleft
	jmp				.nextrow_x2
align 32

.nextrow_x2:
	; First 8 bytes of the row
	
	movq				mm4, [edi]			; load first 8 bytes of pix2 row 
	movq				mm5, [edi+1]		; load bytes 1-8 of pix2 row

	movq				mm2, mm4		; copy mm4 on mm2
	movq				mm3, mm5		; copy mm5 on mm3
	punpcklbw		mm4, mm6		; first 4 bytes of [edi] on mm4
	punpcklbw		mm5, mm6		; first 4 bytes of [edi+1] on mm5
	paddusw			mm4, mm5		; mm4 := first 4 bytes interpolated in words
	psrlw				mm4, 1

	punpckhbw		mm2, mm6		; last 4 bytes of [edi] on mm2
	punpckhbw		mm3, mm6		; last 4 bytes of [edi+1] on mm3
	paddusw			mm2, mm3		; mm2 := last 4 bytes interpolated in words
	psrlw				mm2, 1
	
	packuswb			mm4, mm2 	; pack 8 bytes interpolated on mm4
	movq				mm5,[esi]	; load first 8 bytes of pix1 row
	
	movq				mm3, mm4		; mm4 := abs(mm4-mm5)
	psubusb			mm4, mm5
	psubusb			mm5, mm3
	por				mm4, mm5
		
	; Last 8 bytes of the row	

	movq	mm7, [edi+8]			; load last 8 bytes of pix2 row 
	movq	mm5, [edi+9]			; load bytes 10-17 of pix2 row

	movq				mm2, mm7		; copy mm7 on mm2
	movq				mm3, mm5		; copy mm5 on mm3
	punpcklbw		mm7, mm6		; first 4 bytes of [edi+8] on mm7
	punpcklbw		mm5, mm6		; first 4 bytes of [edi+9] on mm5
	paddusw			mm7, mm5		; mm1 := first 4 bytes interpolated in words
	psrlw				mm7, 1

	punpckhbw		mm2, mm6		; last 4 bytes of [edi] on mm2
	punpckhbw		mm3, mm6		; last 4 bytes of [edi+1] on mm3
	paddusw			mm2, mm3		; mm2 := last 4 bytes interpolated in words
	psrlw				mm2, 1
	
	packuswb			mm7, mm2 	; pack 8 bytes interpolated on mm1
	movq				mm5,[esi+8]	; load last 8 bytes of pix1 row
	
	movq				mm3, mm7		; mm7 := abs(mm1-mm5)
	psubusb			mm7, mm5
	psubusb			mm5, mm3
	por				mm7, mm5
	
	; Now mm4 and mm7 have 16 absdiffs to add
	
	movq				mm3, mm4		; Make copies of these bytes
	movq				mm2, mm7
		
	punpcklbw		mm4, mm6		; Unpack to words and add
	punpcklbw		mm7, mm6
	paddusw			mm4, mm7
	paddusw			mm0, mm4		; Add to the acumulator (mm0)
	
	punpckhbw		mm3, mm6		; Unpack to words and add
	punpckhbw		mm2, mm6
	paddusw			mm3, mm2
	paddusw			mm1, mm3		; Add to the acumulator (mm1)
	
	; Loop termination
	
	add				esi, edx		; update pointers to next row
	add				edi, edx		
			
	sub				ecx,1
	test				ecx, ecx		; check rowsleft
	jnz				near .nextrow_x2
	
	paddusw			mm0, mm1 
	
	movq				mm1, mm0		; Copy mm0 to mm1
	psrlq				mm1, 32
	paddusw			mm0, mm1		; Add 
	movq				mm2, mm0
	psrlq				mm2, 16
	paddusw			mm0, mm2
	movd				eax, mm0		; Store return value
	and				eax, 0xffff

	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp							; restore stack pointer

	emms								; clear mmx registers
	ret								; return
	
global pix_abs16x16_y2_mmx

; int  pix_abs16x16_y2_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ebx = p2 + lx
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = distance accumulators (4 words) 
; mm2 = temp 
; mm3 = temp
; mm4 = temp
; mm5 = temp 
; mm6 = 0
; mm7 = temp


align 32
pix_abs16x16_y2_mmx:
	push				ebp					; save frame pointer
	mov				ebp, esp

	push				ebx					; Saves registers (called saves convention in
	push				ecx					; x86 GCC it seems)
	push				edx					; 
	push				esi
	push				edi
		
	pxor				mm0, mm0				; zero acculumators
	pxor				mm1, mm1
	pxor				mm6, mm6
	mov				esi, [ebp+8]		; get pix1
	mov				edi, [ebp+12]		; get pix2
	mov				edx, [ebp+16]		; get lx
	mov				ecx, [ebp+20]		; get rowsleft
	mov				ebx, edi
	add				ebx, edx
	jmp				.nextrow_y2
align 32

.nextrow_y2:
	; First 8 bytes of the row
	
	movq				mm4, [edi]			; load first 8 bytes of pix2 row 
	movq				mm5, [ebx]			; load bytes 1-8 of pix2 row

	movq				mm2, mm4		; copy mm4 on mm2
	movq				mm3, mm5		; copy mm5 on mm3
	punpcklbw		mm4, mm6		; first 4 bytes of [edi] on mm4
	punpcklbw		mm5, mm6		; first 4 bytes of [ebx] on mm5
	paddusw			mm4, mm5		; mm4 := first 4 bytes interpolated in words
	psrlw				mm4, 1

	punpckhbw		mm2, mm6		; last 4 bytes of [edi] on mm2
	punpckhbw		mm3, mm6		; last 4 bytes of [edi+1] on mm3
	paddusw			mm2, mm3		; mm2 := last 4 bytes interpolated in words
	psrlw				mm2, 1
	
	packuswb			mm4, mm2 	; pack 8 bytes interpolated on mm4
	movq				mm5,[esi]	; load first 8 bytes of pix1 row
	
	movq				mm3, mm4		; mm4 := abs(mm4-mm5)
	psubusb			mm4, mm5
	psubusb			mm5, mm3
	por				mm4, mm5
		
	; Last 8 bytes of the row	

	movq	mm7, [edi+8]			; load last 8 bytes of pix2 row 
	movq	mm5, [ebx+8]			; load bytes 10-17 of pix2 row

	movq				mm2, mm7		; copy mm7 on mm2
	movq				mm3, mm5		; copy mm5 on mm3
	punpcklbw		mm7, mm6		; first 4 bytes of [edi+8] on mm7
	punpcklbw		mm5, mm6		; first 4 bytes of [ebx+8] on mm5
	paddusw			mm7, mm5		; mm1 := first 4 bytes interpolated in words
	psrlw				mm7, 1

	punpckhbw		mm2, mm6		; last 4 bytes of [edi+8] on mm2
	punpckhbw		mm3, mm6		; last 4 bytes of [ebx+8] on mm3
	paddusw			mm2, mm3		; mm2 := last 4 bytes interpolated in words
	psrlw				mm2, 1
	
	packuswb			mm7, mm2 	; pack 8 bytes interpolated on mm1
	movq				mm5,[esi+8]	; load last 8 bytes of pix1 row
	
	movq				mm3, mm7		; mm7 := abs(mm1-mm5)
	psubusb			mm7, mm5
	psubusb			mm5, mm3
	por				mm7, mm5
	
	; Now mm4 and mm7 have 16 absdiffs to add
	
	movq				mm3, mm4		; Make copies of these bytes
	movq				mm2, mm7
		
	punpcklbw		mm4, mm6		; Unpack to words and add
	punpcklbw		mm7, mm6
	paddusw			mm4, mm7
	paddusw			mm0, mm4		; Add to the acumulator (mm0)
	
	punpckhbw		mm3, mm6		; Unpack to words and add
	punpckhbw		mm2, mm6
	paddusw			mm3, mm2
	paddusw			mm1, mm3		; Add to the acumulator (mm1)
	
	; Loop termination
	
	add				esi, edx		; update pointers to next row
	add				edi, edx		
	add				ebx, edx		
	sub				ecx,1
	test				ecx, ecx		; check rowsleft
	jnz				near .nextrow_y2
	
	paddusw			mm0, mm1 
	
	movq				mm1, mm0		; Copy mm0 to mm1
	psrlq				mm1, 32
	paddusw			mm0, mm1		; Add 
	movq				mm2, mm0
	psrlq				mm2, 16
	paddusw			mm0, mm2
	movd				eax, mm0		; Store return value
	and				eax, 0xffff

	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp							; restore stack pointer

	emms								; clear mmx registers
	ret								; return
	
global pix_abs16x16_xy2_mmx

; int pix_abs16x16_xy2_mmx(unsigned char *p1,unsigned char *p2,int lx,int h);

; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ebx = p1+lx
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = bytes p2
; mm2 = bytes p1
; mm3 = bytes p1+lx
; I'd love to find someplace to stash p1+1 and p1+lx+1's bytes
; but I don't think thats going to happen in iA32-land...
; mm4 = temp 4 bytes in words interpolating p1, p1+1
; mm5 = temp 4 bytes in words from p2
; mm6 = temp comparison bit mask p1,p2
; mm7 = temp comparison bit mask p2,p1


align 32
pix_abs16x16_xy2_mmx:
	push ebp		; save stack pointer
	mov ebp, esp	; so that we can do this

	push ebx		; Saves registers (called saves convention in
	push ecx		; x86 GCC it seems)
	push edx		; 
	push esi
	push edi
		
	pxor mm0, mm0				; zero acculumators

	mov esi, [ebp+12]			; get p1
	mov edi, [ebp+8]			; get p2
	mov edx, [ebp+16]			; get lx
	mov ecx, [ebp+20]			; rowsleft := h
	mov ebx, esi
    add ebx, edx		
	jmp .nextrowmm11					; snap to it
align 32
.nextrowmm11:

		;; 
		;; First 8 bytes of row
		;; 
		
		;; First 4 bytes of 8

	movq mm4, [esi]             ; mm4 := first 4 bytes p1
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [ebx]			    ;  mm6 := first 4 bytes p1+lx
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              


	movq mm5, [esi+1]			; mm5 := first 4 bytes p1+1
	punpcklbw mm5, mm7            ;  First 4 bytes p1 in Words...
	paddw mm4, mm5		
	movq mm6, [ebx+1]           ;  mm6 := first 4 bytes p1+lx+1
	punpcklbw mm6, mm7
	paddw mm4, mm6

	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words
		
	movq mm5, [edi]				; mm5:=first 4 bytes of p2 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
	paddw mm4, mm6          

	movq mm5, [esi+1]			; mm5 := first 4 bytes p1+1
	punpckhbw mm5, mm7          ;  First 4 bytes p1 in Words...
	paddw mm4, mm5
	movq mm6, [ebx+1]           ;  mm6 := first 4 bytes p1+lx+1
	punpckhbw mm6, mm7
	paddw mm4, mm6

	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words
		
	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


 		;;
		;; Second 8 bytes of row
		;; 
		;; First 4 bytes of 8

	movq mm4, [esi+8]             ; mm4 := first 4 bytes p1+8
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [ebx+8]			    ;  mm6 := first 4 bytes p1+lx+8
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              


	movq mm5, [esi+9]			; mm5 := first 4 bytes p1+9
	punpcklbw mm5, mm7            ;  First 4 bytes p1 in Words...
	paddw mm4, mm5
	movq mm6, [ebx+9]           ;  mm6 := first 4 bytes p1+lx+9
	punpcklbw mm6, mm7
	paddw mm4, mm6

	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words
		
	movq mm5, [edi+8]				; mm5:=first 4 bytes of p2+8 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
	paddw mm4, mm6          

	movq mm5, [esi+9]			; mm5 := first 4 bytes p1+1
	punpckhbw mm5, mm7          ;  First 4 bytes p1 in Words...
	paddw mm4, mm5	
	movq mm6, [ebx+9]           ;  mm6 := first 4 bytes p1+lx+1
	punpckhbw mm6, mm7
	paddw mm4, mm6
		
	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words

	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


		;;
		;;	Loop termination condition... and stepping
		;;		

	add esi, edx		; update pointer to next row
	add edi, edx		; ditto
	add ebx, edx

	sub  ecx,1
	test ecx, ecx		; check rowsleft
	jnz near .nextrowmm11
		
		;; Sum the Accumulators
	movq  mm4, mm0
	psrlq mm4, 32
	paddw mm0, mm4
	movq  mm6, mm0
	psrlq mm6, 16
	paddw mm0, mm6
	movd eax, mm0		; store return value
	and  eax, 0xffff
		
	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret			; we now return you to your regular programming


