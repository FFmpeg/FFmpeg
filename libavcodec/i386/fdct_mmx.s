; //////////////////////////////////////////////////////////////////////////////
; //
; //  fdctam32.c - AP922 MMX(3D-Now) forward-DCT
; //  ----------
; //  Intel Application Note AP-922 - fast, precise implementation of DCT
; //        http://developer.intel.com/vtune/cbts/appnotes.htm
; //  ----------
; //  
; //       This routine can use a 3D-Now/MMX enhancement to increase the
; //  accuracy of the fdct_col_4 macro.  The dct_col function uses 3D-Now's
; //  PMHULHRW instead of MMX's PMHULHW(and POR).  The substitution improves
; //  accuracy very slightly with performance penalty.  If the target CPU
; //  does not support 3D-Now, then this function cannot be executed.
; //  
; //  For a fast, precise MMX implementation of inverse-DCT 
; //              visit http://www.elecard.com/peter
; //
; //  v1.0 07/22/2000 (initial release)
; //     
; //  liaor@iname.com  http://members.tripod.com/~liaor  
; //////////////////////////////////////////////////////////////////////////////

;;;
;;; A.Stevens Jul 2000:	 ported to nasm syntax and disentangled from
;;; from Win**** compiler specific stuff.
;;; All the real work was done above though.
;;; See above for how to optimise quality on 3DNow! CPU's

		;;
		;;		Macros for code-readability...
		;; 
%define INP eax		;	 pointer to (short *blk) 
%define OUT ecx		;	 pointer to output (temporary store space qwTemp[])
%define TABLE ebx	; pointer to tab_frw_01234567[]
%define TABLEF ebx  ; pointer to tg_all_16
%define round_frw_row edx


%define x0 INP + 0*16
%define x1 INP + 1*16
%define x2 INP + 2*16
%define x3 INP + 3*16
%define x4 INP + 4*16
%define x5 INP + 5*16
%define x6 INP + 6*16
%define x7 INP + 7*16
%define y0 OUT + 0*16
%define y1 OUT + 1*16
%define y2 OUT + 2*16
%define y3 OUT + 3*16
%define y4 OUT + 4*16
%define y5 OUT + 5*16
%define y6 OUT + 6*16
%define y7 OUT + 7*16
				
		;;
		;; Constants for DCT
		;;
%define BITS_FRW_ACC	3 ; 2 or 3 for accuracy
%define SHIFT_FRW_COL	BITS_FRW_ACC
%define SHIFT_FRW_ROW	(BITS_FRW_ACC + 17)
%define RND_FRW_ROW		(1 << (SHIFT_FRW_ROW-1))
%define RND_FRW_COL		(1 << (SHIFT_FRW_COL-1))

extern fdct_one_corr		
extern fdct_r_row				;  Defined in C for convenience
		;;
		;; Concatenated table of forward dct transformation coeffs.
		;; 
extern  fdct_tg_all_16			; Defined in C for convenience
		;; Offsets into table..
		
%define tg_1_16 (TABLEF + 0)
%define tg_2_16 (TABLEF + 8)
%define tg_3_16 (TABLEF + 16)
%define cos_4_16 (TABLEF + 24)
%define ocos_4_16 (TABLEF + 32)		

		;;
		;; Concatenated table of forward dct coefficients
		;; 
extern tab_frw_01234567		; Defined in C for convenience

		;; Offsets into table..
SECTION .text
		
global fdct_mmx
		
;;; 
;;; void fdct_mmx( short *blk )
;;; 



;     ////////////////////////////////////////////////////////////////////////
;     //
;     // The high-level pseudocode for the fdct_am32() routine :
;     //
;     // fdct_am32()
;     // {
;     //    forward_dct_col03(); // dct_column transform on cols 0-3
;     //    forward_dct_col47(); // dct_column transform on cols 4-7
;     //    for ( j = 0; j < 8; j=j+1 )
;     //      forward_dct_row1(j); // dct_row transform on row #j
;     // }
;     //
;    

align 32
fdct_mmx:
	push ebp			; save stack pointer
	mov ebp, esp		; link

	push ebx		
	push ecx		
	push edx
	push edi
					
	mov INP, [ebp+8];		; input data is row 0 of blk[]
    ;// transform the left half of the matrix (4 columns)

    lea TABLEF,  [fdct_tg_all_16];
    mov OUT, INP;

;	lea round_frw_col,  [r_frw_col]
    ; for ( i = 0; i < 2; i = i + 1)
    ; the for-loop is executed twice.  We are better off unrolling the 
    ; loop to avoid branch misprediction.
.mmx32_fdct_col03: 
    movq mm0, [x1] ; 0 ; x1
     ;;

    movq mm1, [x6] ; 1 ; x6
    movq mm2, mm0 ; 2 ; x1

    movq mm3, [x2] ; 3 ; x2
    paddsw mm0, mm1 ; t1 = x[1] + x[6]

    movq mm4, [x5] ; 4 ; x5
    psllw mm0, SHIFT_FRW_COL ; t1

    movq mm5, [x0] ; 5 ; x0
    paddsw mm4, mm3 ; t2 = x[2] + x[5]

    paddsw mm5, [x7] ; t0 = x[0] + x[7]
    psllw mm4, SHIFT_FRW_COL ; t2

    movq mm6, mm0 ; 6 ; t1
    psubsw mm2, mm1 ; 1 ; t6 = x[1] - x[6]

    movq mm1,  [tg_2_16] ; 1 ; tg_2_16
    psubsw mm0, mm4 ; tm12 = t1 - t2

    movq mm7, [x3] ; 7 ; x3
    pmulhw mm1, mm0 ; tm12*tg_2_16

    paddsw mm7, [x4] ; t3 = x[3] + x[4]
    psllw mm5, SHIFT_FRW_COL ; t0

    paddsw mm6, mm4 ; 4 ; tp12 = t1 + t2
    psllw mm7, SHIFT_FRW_COL ; t3

    movq mm4, mm5 ; 4 ; t0
    psubsw mm5, mm7 ; tm03 = t0 - t3

    paddsw mm1, mm5 ; y2 = tm03 + tm12*tg_2_16
    paddsw mm4, mm7 ; 7 ; tp03 = t0 + t3

    por mm1,  [fdct_one_corr] ; correction y2 +0.5
    psllw mm2, SHIFT_FRW_COL+1 ; t6

    pmulhw mm5,  [tg_2_16] ; tm03*tg_2_16
    movq mm7, mm4 ; 7 ; tp03

    psubsw mm3, [x5] ; t5 = x[2] - x[5]
    psubsw mm4, mm6 ; y4 = tp03 - tp12

    movq [y2], mm1 ; 1 ; save y2
    paddsw mm7, mm6 ; 6 ; y0 = tp03 + tp12
    
    movq mm1, [x3] ; 1 ; x3
    psllw mm3, SHIFT_FRW_COL+1 ; t5

    psubsw mm1, [x4] ; t4 = x[3] - x[4]
    movq mm6, mm2 ; 6 ; t6
    
    movq [y4], mm4 ; 4 ; save y4
    paddsw mm2, mm3 ; t6 + t5

    pmulhw mm2,  [ocos_4_16] ; tp65 = (t6 + t5)*cos_4_16
    psubsw mm6, mm3 ; 3 ; t6 - t5

    pmulhw mm6,  [ocos_4_16] ; tm65 = (t6 - t5)*cos_4_16
    psubsw mm5, mm0 ; 0 ; y6 = tm03*tg_2_16 - tm12

    por mm5,  [fdct_one_corr] ; correction y6 +0.5
    psllw mm1, SHIFT_FRW_COL ; t4

    por mm2,  [fdct_one_corr] ; correction tp65 +0.5
    movq mm4, mm1 ; 4 ; t4

    movq mm3, [x0] ; 3 ; x0
    paddsw mm1, mm6 ; tp465 = t4 + tm65

    psubsw mm3, [x7] ; t7 = x[0] - x[7]
    psubsw mm4, mm6 ; 6 ; tm465 = t4 - tm65

    movq mm0,  [tg_1_16] ; 0 ; tg_1_16
    psllw mm3, SHIFT_FRW_COL ; t7

    movq mm6,  [tg_3_16] ; 6 ; tg_3_16
    pmulhw mm0, mm1 ; tp465*tg_1_16

    movq [y0], mm7 ; 7 ; save y0
    pmulhw mm6, mm4 ; tm465*tg_3_16

    movq [y6], mm5 ; 5 ; save y6
    movq mm7, mm3 ; 7 ; t7

    movq mm5,  [tg_3_16] ; 5 ; tg_3_16
    psubsw mm7, mm2 ; tm765 = t7 - tp65

    paddsw mm3, mm2 ; 2 ; tp765 = t7 + tp65
    pmulhw mm5, mm7 ; tm765*tg_3_16

    paddsw mm0, mm3 ; y1 = tp765 + tp465*tg_1_16
    paddsw mm6, mm4 ; tm465*tg_3_16

    pmulhw mm3,  [tg_1_16] ; tp765*tg_1_16
    ;;

    por mm0,  [fdct_one_corr] ; correction y1 +0.5
    paddsw mm5, mm7 ; tm765*tg_3_16

    psubsw mm7, mm6 ; 6 ; y3 = tm765 - tm465*tg_3_16
    add INP, 0x08   ; ; increment pointer

    movq [y1], mm0 ; 0 ; save y1
    paddsw mm5, mm4 ; 4 ; y5 = tm765*tg_3_16 + tm465

    movq [y3], mm7 ; 7 ; save y3
    psubsw mm3, mm1 ; 1 ; y7 = tp765*tg_1_16 - tp465

    movq [y5], mm5 ; 5 ; save y5


.mmx32_fdct_col47: ; begin processing last four columns
    movq mm0, [x1] ; 0 ; x1
    ;;
    movq [y7], mm3 ; 3 ; save y7 (columns 0-4)
    ;;

    movq mm1, [x6] ; 1 ; x6
    movq mm2, mm0 ; 2 ; x1

    movq mm3, [x2] ; 3 ; x2
    paddsw mm0, mm1 ; t1 = x[1] + x[6]

    movq mm4, [x5] ; 4 ; x5
    psllw mm0, SHIFT_FRW_COL ; t1

    movq mm5, [x0] ; 5 ; x0
    paddsw mm4, mm3 ; t2 = x[2] + x[5]

    paddsw mm5, [x7] ; t0 = x[0] + x[7]
    psllw mm4, SHIFT_FRW_COL ; t2

    movq mm6, mm0 ; 6 ; t1
    psubsw mm2, mm1 ; 1 ; t6 = x[1] - x[6]

    movq mm1,  [tg_2_16] ; 1 ; tg_2_16
    psubsw mm0, mm4 ; tm12 = t1 - t2

    movq mm7, [x3] ; 7 ; x3
    pmulhw mm1, mm0 ; tm12*tg_2_16

    paddsw mm7, [x4] ; t3 = x[3] + x[4]
    psllw mm5, SHIFT_FRW_COL ; t0

    paddsw mm6, mm4 ; 4 ; tp12 = t1 + t2
    psllw mm7, SHIFT_FRW_COL ; t3

    movq mm4, mm5 ; 4 ; t0
    psubsw mm5, mm7 ; tm03 = t0 - t3

    paddsw mm1, mm5 ; y2 = tm03 + tm12*tg_2_16
    paddsw mm4, mm7 ; 7 ; tp03 = t0 + t3

    por mm1,  [fdct_one_corr] ; correction y2 +0.5
    psllw mm2, SHIFT_FRW_COL+1 ; t6

    pmulhw mm5,  [tg_2_16] ; tm03*tg_2_16
    movq mm7, mm4 ; 7 ; tp03

    psubsw mm3, [x5] ; t5 = x[2] - x[5]
    psubsw mm4, mm6 ; y4 = tp03 - tp12

    movq [y2+8], mm1 ; 1 ; save y2
    paddsw mm7, mm6 ; 6 ; y0 = tp03 + tp12
    
    movq mm1, [x3] ; 1 ; x3
    psllw mm3, SHIFT_FRW_COL+1 ; t5

    psubsw mm1, [x4] ; t4 = x[3] - x[4]
    movq mm6, mm2 ; 6 ; t6
    
    movq [y4+8], mm4 ; 4 ; save y4
    paddsw mm2, mm3 ; t6 + t5

    pmulhw mm2,  [ocos_4_16] ; tp65 = (t6 + t5)*cos_4_16
    psubsw mm6, mm3 ; 3 ; t6 - t5

    pmulhw mm6,  [ocos_4_16] ; tm65 = (t6 - t5)*cos_4_16
    psubsw mm5, mm0 ; 0 ; y6 = tm03*tg_2_16 - tm12

    por mm5,  [fdct_one_corr] ; correction y6 +0.5
    psllw mm1, SHIFT_FRW_COL ; t4

    por mm2,  [fdct_one_corr] ; correction tp65 +0.5
    movq mm4, mm1 ; 4 ; t4

    movq mm3, [x0] ; 3 ; x0
    paddsw mm1, mm6 ; tp465 = t4 + tm65

    psubsw mm3, [x7] ; t7 = x[0] - x[7]
    psubsw mm4, mm6 ; 6 ; tm465 = t4 - tm65

    movq mm0,  [tg_1_16] ; 0 ; tg_1_16
    psllw mm3, SHIFT_FRW_COL ; t7

    movq mm6,  [tg_3_16] ; 6 ; tg_3_16
    pmulhw mm0, mm1 ; tp465*tg_1_16

    movq [y0+8], mm7 ; 7 ; save y0
    pmulhw mm6, mm4 ; tm465*tg_3_16

    movq [y6+8], mm5 ; 5 ; save y6
    movq mm7, mm3 ; 7 ; t7

    movq mm5,  [tg_3_16] ; 5 ; tg_3_16
    psubsw mm7, mm2 ; tm765 = t7 - tp65

    paddsw mm3, mm2 ; 2 ; tp765 = t7 + tp65
    pmulhw mm5, mm7 ; tm765*tg_3_16

    paddsw mm0, mm3 ; y1 = tp765 + tp465*tg_1_16
    paddsw mm6, mm4 ; tm465*tg_3_16

    pmulhw mm3,  [tg_1_16] ; tp765*tg_1_16
    ;;

    por mm0, [fdct_one_corr] ; correction y1 +0.5
    paddsw mm5, mm7 ; tm765*tg_3_16

    psubsw mm7, mm6 ; 6 ; y3 = tm765 - tm465*tg_3_16
    ;;

    movq [y1+8], mm0 ; 0 ; save y1
    paddsw mm5, mm4 ; 4 ; y5 = tm765*tg_3_16 + tm465

    movq [y3+8], mm7 ; 7 ; save y3
    psubsw mm3, mm1 ; 1 ; y7 = tp765*tg_1_16 - tp465

    movq [y5+8], mm5 ; 5 ; save y5

    movq [y7+8], mm3 ; 3 ; save y7

;    emms;
;    }   ; end of forward_dct_col07() 
    ;  done with dct_row transform

  
  ; fdct_mmx32_cols() --
  ; the following subroutine repeats the row-transform operation, 
  ; except with different shift&round constants.  This version
  ; does NOT transpose the output again.  Thus the final output
  ; is transposed with respect to the source.
  ;
  ;  The output is stored into blk[], which destroys the original
  ;  input data.
	mov INP,  [ebp+8];		;; row 0
	 mov edi, 0x08;	;x = 8

	lea TABLE,  [tab_frw_01234567]; ; row 0
	 mov OUT, INP;

	lea round_frw_row,  [fdct_r_row];
	; for ( x = 8; x > 0; --x )  ; transform one row per iteration

; ---------- loop begin
  .lp_mmx_fdct_row1:
    movd mm5,  [INP+12]; ; mm5 = 7 6

    punpcklwd mm5,  [INP+8] ; mm5 =  5 7 4 6

    movq mm2, mm5;     ; mm2 = 5 7 4 6
    psrlq mm5, 32;     ; mm5 = _ _ 5 7

    movq mm0,  [INP]; ; mm0 = 3 2 1 0
    punpcklwd mm5, mm2;; mm5 = 4 5 6 7

    movq mm1, mm0;     ; mm1 = 3 2 1 0
    paddsw mm0, mm5;   ; mm0 = [3+4, 2+5, 1+6, 0+7] (xt3, xt2, xt1, xt0)

    psubsw mm1, mm5;   ; mm1 = [3-4, 2-5, 1-6, 0-7] (xt7, xt6, xt5, xt4)
    movq mm2, mm0;     ; mm2 = [ xt3 xt2 xt1 xt0 ]

    ;movq [ xt3xt2xt1xt0 ], mm0;
    ;movq [ xt7xt6xt5xt4 ], mm1;

    punpcklwd mm0, mm1;; mm0 = [ xt5 xt1 xt4 xt0 ]

    punpckhwd mm2, mm1;; mm2 = [ xt7 xt3 xt6 xt2 ]
    movq mm1, mm2;     ; mm1

    ;; shuffle bytes around

;  movq mm0,  [INP] ; 0 ; x3 x2 x1 x0

;  movq mm1,  [INP+8] ; 1 ; x7 x6 x5 x4
    movq mm2, mm0 ; 2 ; x3 x2 x1 x0

    movq mm3,  [TABLE] ; 3 ; w06 w04 w02 w00
    punpcklwd mm0, mm1 ; x5 x1 x4 x0

    movq mm5, mm0 ; 5 ; x5 x1 x4 x0
    punpckldq mm0, mm0 ; x4 x0 x4 x0  [ xt2 xt0 xt2 xt0 ]

    movq mm4,  [TABLE+8] ; 4 ; w07 w05 w03 w01
    punpckhwd mm2, mm1 ; 1 ; x7 x3 x6 x2

    pmaddwd mm3, mm0 ; x4*w06+x0*w04 x4*w02+x0*w00
    movq mm6, mm2 ; 6 ; x7 x3 x6 x2

    movq mm1,  [TABLE+32] ; 1 ; w22 w20 w18 w16
    punpckldq mm2, mm2 ; x6 x2 x6 x2  [ xt3 xt1 xt3 xt1 ]

    pmaddwd mm4, mm2 ; x6*w07+x2*w05 x6*w03+x2*w01
    punpckhdq mm5, mm5 ; x5 x1 x5 x1  [ xt6 xt4 xt6 xt4 ]

    pmaddwd mm0,  [TABLE+16] ; x4*w14+x0*w12 x4*w10+x0*w08
    punpckhdq mm6, mm6 ; x7 x3 x7 x3  [ xt7 xt5 xt7 xt5 ]

    movq mm7,  [TABLE+40] ; 7 ; w23 w21 w19 w17
    pmaddwd mm1, mm5 ; x5*w22+x1*w20 x5*w18+x1*w16
;mm3 = a1, a0 (y2,y0)
;mm1 = b1, b0 (y3,y1)
;mm0 = a3,a2  (y6,y4)
;mm5 = b3,b2  (y7,y5)

    paddd mm3,  [round_frw_row] ; +rounder (y2,y0)
    pmaddwd mm7, mm6 ; x7*w23+x3*w21 x7*w19+x3*w17

    pmaddwd mm2,  [TABLE+24] ; x6*w15+x2*w13 x6*w11+x2*w09
    paddd mm3, mm4 ; 4 ; a1=sum(even1) a0=sum(even0) ; now ( y2, y0)

    pmaddwd mm5,  [TABLE+48] ; x5*w30+x1*w28 x5*w26+x1*w24
    ;;

    pmaddwd mm6,  [TABLE+56] ; x7*w31+x3*w29 x7*w27+x3*w25
    paddd mm1, mm7 ; 7 ; b1=sum(odd1) b0=sum(odd0) ; now ( y3, y1)

    paddd mm0,  [round_frw_row] ; +rounder (y6,y4)
    psrad mm3, SHIFT_FRW_ROW ; (y2, y0)

    paddd mm1,  [round_frw_row] ; +rounder (y3,y1)
    paddd mm0, mm2 ; 2 ; a3=sum(even3) a2=sum(even2) ; now (y6, y4)

    paddd mm5,  [round_frw_row] ; +rounder (y7,y5)
    psrad mm1, SHIFT_FRW_ROW ; y1=a1+b1 y0=a0+b0

    paddd mm5, mm6 ; 6 ; b3=sum(odd3) b2=sum(odd2) ; now ( y7, y5)
    psrad mm0, SHIFT_FRW_ROW ;y3=a3+b3 y2=a2+b2

    add OUT, 16;  ; increment row-output address by 1 row
    psrad mm5, SHIFT_FRW_ROW ; y4=a3-b3 y5=a2-b2

    add INP, 16;  ; increment row-address by 1 row
    packssdw mm3, mm0 ; 0 ; y6 y4 y2 y0

    packssdw mm1, mm5 ; 3 ; y7 y5 y3 y1
    movq mm6, mm3;    ; mm0 = y6 y4 y2 y0

    punpcklwd mm3, mm1; ; y3 y2 y1 y0
    sub edi, 0x01;   ; i = i - 1
    
    punpckhwd mm6, mm1; ; y7 y6 y5 y4
    add TABLE,64;  ; increment to next table

    movq  [OUT-16], mm3 ; 1 ; save y3 y2 y1 y0

    movq  [OUT-8], mm6 ; 7 ; save y7 y6 y5 y4

    cmp edi, 0x00;
    jg near .lp_mmx_fdct_row1;  ; begin fdct processing on next row
		;; 
		;; Tidy up and return
		;;
	pop edi
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp			; restore stack pointer
	emms
	ret		
  