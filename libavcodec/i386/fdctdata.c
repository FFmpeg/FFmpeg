//////////////////////////////////////////////////////////////////////////////
//
//  fdctam32.c - AP922 MMX(3D-Now) forward-DCT
//  ----------
//  Intel Application Note AP-922 - fast, precise implementation of DCT
//        http://developer.intel.com/vtune/cbts/appnotes.htm
//  ----------
//  
//       This routine uses a 3D-Now/MMX enhancement to increase the
//  accuracy of the fdct_col_4 macro.  The dct_col function uses 3D-Now's
//  PMHULHRW instead of MMX's PMHULHW(and POR).  The substitution improves
//  accuracy very slightly with performance penalty.  If the target CPU
//  does not support 3D-Now, then this function cannot be executed.  
//  fdctmm32.c contains the standard MMX implementation of AP-922.
//  
//  For a fast, precise MMX implementation of inverse-DCT 
//              visit http://www.elecard.com/peter
//
//  v1.0 07/22/2000 (initial release)
//       Initial release of AP922 MMX(3D-Now) forward_DCT.
//       This code was tested with Visual C++ 6.0Pro + service_pack4 + 
//       processor_pack_beta!  If you have the processor_pack_beta, you can
//       remove the #include for amd3dx.h, and substitute the 'normal'
//       assembly lines for the macro'd versions.  Otherwise, this
//       code should compile 'as is', under Visual C++ 6.0 Pro.
//     
//  liaor@iname.com  http://members.tripod.com/~liaor  
//////////////////////////////////////////////////////////////////////////////

#include <inttypes.h>

//////////////////////////////////////////////////////////////////////
//
// constants for the forward DCT
// -----------------------------
//
// Be sure to check that your compiler is aligning all constants to QWORD
// (8-byte) memory boundaries!  Otherwise the unaligned memory access will
// severely stall MMX execution.
//
//////////////////////////////////////////////////////////////////////

#define BITS_FRW_ACC	3 //; 2 or 3 for accuracy
#define SHIFT_FRW_COL	BITS_FRW_ACC
#define SHIFT_FRW_ROW	(BITS_FRW_ACC + 17)
//#define RND_FRW_ROW		(262144 * (BITS_FRW_ACC - 1)) //; 1 << (SHIFT_FRW_ROW-1)
#define RND_FRW_ROW		(1 << (SHIFT_FRW_ROW-1))
//#define RND_FRW_COL		(2 * (BITS_FRW_ACC - 1)) //; 1 << (SHIFT_FRW_COL-1)
#define RND_FRW_COL		(1 << (SHIFT_FRW_COL-1))

//concatenated table, for forward DCT transformation
const int16_t fdct_tg_all_16[] = {
	13036, 13036, 13036, 13036,		// tg * (2<<16) + 0.5
	27146, 27146, 27146, 27146,		// tg * (2<<16) + 0.5
	-21746, -21746, -21746, -21746,	// tg * (2<<16) + 0.5
	-19195, -19195, -19195, -19195,	//cos * (2<<16) + 0.5
	23170, 23170, 23170, 23170 };	//cos * (2<<15) + 0.5
const long long  fdct_one_corr = 0x0001000100010001LL;
const long fdct_r_row[2] = {RND_FRW_ROW, RND_FRW_ROW };

const int16_t tab_frw_01234567[] = {  // forward_dct coeff table
    //row0
    16384, 16384, 21407, -8867,     //    w09 w01 w08 w00
    16384, 16384, 8867, -21407,     //    w13 w05 w12 w04
    16384, -16384, 8867, 21407,     //    w11 w03 w10 w02
    -16384, 16384, -21407, -8867,   //    w15 w07 w14 w06
    22725, 12873, 19266, -22725,    //    w22 w20 w18 w16
    19266, 4520, -4520, -12873,     //    w23 w21 w19 w17
    12873, 4520, 4520, 19266,       //    w30 w28 w26 w24
    -22725, 19266, -12873, -22725,  //    w31 w29 w27 w25

    //row1
    22725, 22725, 29692, -12299,    //    w09 w01 w08 w00
    22725, 22725, 12299, -29692,    //    w13 w05 w12 w04
    22725, -22725, 12299, 29692,    //    w11 w03 w10 w02
    -22725, 22725, -29692, -12299,  //    w15 w07 w14 w06
    31521, 17855, 26722, -31521,    //    w22 w20 w18 w16
    26722, 6270, -6270, -17855,     //    w23 w21 w19 w17
    17855, 6270, 6270, 26722,       //    w30 w28 w26 w24
    -31521, 26722, -17855, -31521,  //    w31 w29 w27 w25

    //row2
    21407, 21407, 27969, -11585,    //    w09 w01 w08 w00
    21407, 21407, 11585, -27969,    //    w13 w05 w12 w04
    21407, -21407, 11585, 27969,    //    w11 w03 w10 w02
    -21407, 21407, -27969, -11585,  //    w15 w07 w14 w06
    29692, 16819, 25172, -29692,    //    w22 w20 w18 w16
    25172, 5906, -5906, -16819,     //    w23 w21 w19 w17
    16819, 5906, 5906, 25172,       //    w30 w28 w26 w24
    -29692, 25172, -16819, -29692,  //    w31 w29 w27 w25

    //row3
    19266, 19266, 25172, -10426,    //    w09 w01 w08 w00
    19266, 19266, 10426, -25172,    //    w13 w05 w12 w04
    19266, -19266, 10426, 25172,    //    w11 w03 w10 w02
    -19266, 19266, -25172, -10426,  //    w15 w07 w14 w06, 
    26722, 15137, 22654, -26722,    //    w22 w20 w18 w16
    22654, 5315, -5315, -15137,     //    w23 w21 w19 w17
    15137, 5315, 5315, 22654,       //    w30 w28 w26 w24
    -26722, 22654, -15137, -26722,  //    w31 w29 w27 w25, 

    //row4
    16384, 16384, 21407, -8867,     //    w09 w01 w08 w00
    16384, 16384, 8867, -21407,     //    w13 w05 w12 w04
    16384, -16384, 8867, 21407,     //    w11 w03 w10 w02
    -16384, 16384, -21407, -8867,   //    w15 w07 w14 w06
    22725, 12873, 19266, -22725,    //    w22 w20 w18 w16
    19266, 4520, -4520, -12873,     //    w23 w21 w19 w17
    12873, 4520, 4520, 19266,       //    w30 w28 w26 w24
    -22725, 19266, -12873, -22725,  //    w31 w29 w27 w25 

    //row5
    19266, 19266, 25172, -10426,    //    w09 w01 w08 w00
    19266, 19266, 10426, -25172,    //    w13 w05 w12 w04
    19266, -19266, 10426, 25172,    //    w11 w03 w10 w02
    -19266, 19266, -25172, -10426,  //    w15 w07 w14 w06
    26722, 15137, 22654, -26722,    //    w22 w20 w18 w16
    22654, 5315, -5315, -15137,     //    w23 w21 w19 w17
    15137, 5315, 5315, 22654,       //    w30 w28 w26 w24
    -26722, 22654, -15137, -26722,  //    w31 w29 w27 w25

    //row6
    21407, 21407, 27969, -11585,    //    w09 w01 w08 w00
    21407, 21407, 11585, -27969,    //    w13 w05 w12 w04
    21407, -21407, 11585, 27969,    //    w11 w03 w10 w02
    -21407, 21407, -27969, -11585,  //    w15 w07 w14 w06, 
    29692, 16819, 25172, -29692,    //    w22 w20 w18 w16
    25172, 5906, -5906, -16819,     //    w23 w21 w19 w17
    16819, 5906, 5906, 25172,       //    w30 w28 w26 w24
    -29692, 25172, -16819, -29692,  //    w31 w29 w27 w25, 

    //row7
    22725, 22725, 29692, -12299,    //    w09 w01 w08 w00
    22725, 22725, 12299, -29692,    //    w13 w05 w12 w04
    22725, -22725, 12299, 29692,    //    w11 w03 w10 w02
    -22725, 22725, -29692, -12299,  //    w15 w07 w14 w06, 
    31521, 17855, 26722, -31521,    //    w22 w20 w18 w16
    26722, 6270, -6270, -17855,     //    w23 w21 w19 w17
    17855, 6270, 6270, 26722,       //    w30 w28 w26 w24
    -31521, 26722, -17855, -31521   //    w31 w29 w27 w25
};


