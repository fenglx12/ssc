/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.  
 *
 * Copyright (c) 2010-2013, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     TComRom.h
    \brief    global variables & functions (header)
*/

#ifndef __TCOMROM__
#define __TCOMROM__

#include "CommonDef.h"

#include<stdio.h>
#include<iostream>

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Macros
// ====================================================================================================================

#define     MAX_CU_DEPTH             6                          // log2(LCUSize)
#define     MAX_CU_SIZE             (1<<(MAX_CU_DEPTH))         // maximum allowable size of CU, surely 64? (not 1<<7 = 128)
#define     MIN_PU_SIZE              4
#define     MIN_TU_SIZE              4
#define     MAX_TU_SIZE             32
#define     MAX_NUM_SPU_W           (MAX_CU_SIZE/MIN_PU_SIZE)   // maximum number of SPU in horizontal line

#define     SCALING_LIST_REM_NUM     6

// ====================================================================================================================
// Initialize / destroy functions
// ====================================================================================================================

Void         initROM();
Void         destroyROM();

// ====================================================================================================================
// Data structure related table & variable
// ====================================================================================================================

// flexible conversion from relative to absolute index
extern       UInt   g_auiZscanToRaster[ MAX_NUM_SPU_W*MAX_NUM_SPU_W ];
extern       UInt   g_auiRasterToZscan[ MAX_NUM_SPU_W*MAX_NUM_SPU_W ];
extern       UInt*  g_scanOrder[SCAN_NUMBER_OF_GROUP_TYPES][SCAN_NUMBER_OF_TYPES][ MAX_CU_DEPTH ][ MAX_CU_DEPTH ];

Void         initZscanToRaster ( Int iMaxDepth, Int iDepth, UInt uiStartVal, UInt*& rpuiCurrIdx );
Void         initRasterToZscan ( UInt uiMaxCUWidth, UInt uiMaxCUHeight, UInt uiMaxDepth         );

// conversion of partition index to picture pel position
extern       UInt   g_auiRasterToPelX[ MAX_NUM_SPU_W*MAX_NUM_SPU_W ];
extern       UInt   g_auiRasterToPelY[ MAX_NUM_SPU_W*MAX_NUM_SPU_W ];

Void         initRasterToPelXY ( UInt uiMaxCUWidth, UInt uiMaxCUHeight, UInt uiMaxDepth );

// global variable (LCU width/height, max. CU depth)
extern       UInt g_uiMaxCUWidth;
extern       UInt g_uiMaxCUHeight;
extern       UInt g_uiMaxCUDepth;
extern       UInt g_uiAddCUDepth;

#if !RExt__N0288_SPECIFY_TRANSFORM_SKIP_MAXIMUM_SIZE
#define MAX_TS_WIDTH  4
#define MAX_TS_HEIGHT 8
#endif

extern       UInt g_auiPUOffset[NUMBER_OF_PART_SIZES];

#define QUANT_SHIFT                14 // Q(4) = 2^14
#define IQUANT_SHIFT                6
#define SCALE_BITS                 15 // Inherited from TMuC, pressumably for fractional bit estimates in RDOQ
#if RExt__N0188_EXTENDED_PRECISION_PROCESSING
extern Int g_maxTrDynamicRange[MAX_NUM_CHANNEL_TYPE];
#else
#define MAX_TR_DYNAMIC_RANGE       15 // Maximum input forward transform dynamic range (excluding sign bit)
#define TRANSFORM_MAXIMUM          ((1 << MAX_TR_DYNAMIC_RANGE) - 1)
#define TRANSFORM_MINIMUM          (-(1 << MAX_TR_DYNAMIC_RANGE))
#endif

#define SQRT2                      11585
#define SQRT2_SHIFT                13
#define INVSQRT2                   11585
#define INVSQRT2_SHIFT             14
#define ADDITIONAL_MULTIPLIER_BITS 14

#define SHIFT_INV_1ST               7 // Shift after first inverse transform stage
#define SHIFT_INV_2ND              12 // Shift after second inverse transform stage

extern Int g_quantScales[SCALING_LIST_REM_NUM];             // Q(QP%6)  
extern Int g_invQuantScales[SCALING_LIST_REM_NUM];          // IQ(QP%6)

#if RExt__INDEPENDENT_FORWARD_AND_INVERSE_TRANSFORMS
#if RExt__HIGH_PRECISION_FORWARD_TRANSFORM
static const Int g_transformMatrixShift[TRANSFORM_NUMBER_OF_DIRECTIONS] = { 14, 6 };
#else
static const Int g_transformMatrixShift[TRANSFORM_NUMBER_OF_DIRECTIONS] = {  6, 6 };
#endif
#else
#define TRANSFORM_MATRIX_SHIFT 6 //NOTE: RExt - This value does not directly affect the transform matrices and must be adjusted in line with any change made to them
#endif

#if RExt__INDEPENDENT_FORWARD_AND_INVERSE_TRANSFORMS
extern const TMatrixCoeff g_aiT4 [TRANSFORM_NUMBER_OF_DIRECTIONS][4][4];
extern const TMatrixCoeff g_aiT8 [TRANSFORM_NUMBER_OF_DIRECTIONS][8][8];
extern const TMatrixCoeff g_aiT16[TRANSFORM_NUMBER_OF_DIRECTIONS][16][16];
extern const TMatrixCoeff g_aiT32[TRANSFORM_NUMBER_OF_DIRECTIONS][32][32];
#else
extern const TMatrixCoeff g_aiT4[4][4];
extern const TMatrixCoeff g_aiT8[8][8];
extern const TMatrixCoeff g_aiT16[16][16];
extern const TMatrixCoeff g_aiT32[32][32];
#endif

// ====================================================================================================================
// Luma QP to Chroma QP mapping
// ====================================================================================================================

static const Int chromaQPMappingTableSize = 58;

extern const UChar  g_aucChromaScale[NUM_CHROMA_FORMAT][chromaQPMappingTableSize];

// ====================================================================================================================
// Entropy Coding
// ====================================================================================================================

#define CONTEXT_STATE_BITS             6
#define LAST_SIGNIFICANT_GROUPS       10
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_1026
#define MAXIMUM_GOLOMB_RICE_PARAMETER  5
#endif

// ====================================================================================================================
// Scanning order & context mapping table
// ====================================================================================================================

extern const UInt   ctxIndMap4x4[4*4];

extern const UInt   g_uiGroupIdx[ MAX_TU_SIZE ];
extern const UInt   g_uiMinInGroup[ LAST_SIGNIFICANT_GROUPS ];

#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_1026
extern const UInt   g_auiGoRiceRange[MAXIMUM_GOLOMB_RICE_PARAMETER];                  //!< maximum value coded with Rice codes
extern const UInt   g_auiGoRicePrefixLen[MAXIMUM_GOLOMB_RICE_PARAMETER];              //!< prefix length for each maximum value
#endif

// ====================================================================================================================
// ADI table
// ====================================================================================================================

extern const UChar  g_aucIntraModeNumFast[MAX_CU_DEPTH];

extern const UChar  g_chroma422IntraAngleMappingTable[NUM_INTRA_MODE];

// ====================================================================================================================
// Bit-depth
// ====================================================================================================================

extern        Int g_bitDepth   [MAX_NUM_CHANNEL_TYPE];
extern        Int g_PCMBitDepth[MAX_NUM_CHANNEL_TYPE];

// ====================================================================================================================
// Mode-Dependent DST Matrices
// ====================================================================================================================

#if RExt__INDEPENDENT_FORWARD_AND_INVERSE_TRANSFORMS
extern const TMatrixCoeff g_as_DST_MAT_4 [TRANSFORM_NUMBER_OF_DIRECTIONS][4][4];
#else
extern const TMatrixCoeff g_as_DST_MAT_4 [4][4];
#endif

// ====================================================================================================================
// Misc.
// ====================================================================================================================

extern       Char   g_aucConvertToBit  [ MAX_CU_SIZE+1 ];   // from width to log2(width)-2

#ifndef ENC_DEC_TRACE
#define ENC_DEC_TRACE 0
#endif


#if ENC_DEC_TRACE
extern FILE*  g_hTrace;
extern Bool   g_bJustDoIt;
extern const Bool g_bEncDecTraceEnable;
extern const Bool g_bEncDecTraceDisable;
extern Bool   g_HLSTraceEnable;
extern UInt64 g_nSymbolCounter;

#define COUNTER_START    1
#define COUNTER_END      0 //( UInt64(1) << 63 )

#define DTRACE_CABAC_F(x)     if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  "%f", x );
#define DTRACE_CABAC_V(x)     if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  "%d", x );
#define DTRACE_CABAC_VL(x)    if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  "%lld", x );
#define DTRACE_CABAC_T(x)     if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  "%s", x );
#define DTRACE_CABAC_X(x)     if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  "%x", x );
#define DTRACE_CABAC_R( x,y ) if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  x,    y );
#define DTRACE_CABAC_N        if ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) printf(  "\n"    );

#else

#define DTRACE_CABAC_F(x)
#define DTRACE_CABAC_V(x)
#define DTRACE_CABAC_VL(x)
#define DTRACE_CABAC_T(x)
#define DTRACE_CABAC_X(x)
#define DTRACE_CABAC_R( x,y )
#define DTRACE_CABAC_N

#endif


#define SCALING_LIST_NUM (MAX_NUM_COMPONENT * NUMBER_OF_PREDICTION_MODES) ///< list number for quantization matrix

#define SCALING_LIST_START_VALUE 8                                        ///< start value for dpcm mode
#define MAX_MATRIX_COEF_NUM 64                                            ///< max coefficient number for quantization matrix
#define MAX_MATRIX_SIZE_NUM 8                                             ///< max size number for quantization matrix
#define SCALING_LIST_BITS 8                                               ///< bit depth of scaling list entries
#define LOG2_SCALING_LIST_NEUTRAL_VALUE 4                                 ///< log2 of the value that, when used in a scaling list, has no effect on quantisation
#define SCALING_LIST_DC 16                                                ///< default DC value

extern const char *MatrixType[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM];
extern const char *MatrixType_DC[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM];

extern Int g_quantTSDefault4x4[4*4];
extern Int g_quantIntraDefault8x8[8*8];
extern Int g_quantInterDefault8x8[8*8];

extern UInt g_scalingListSize [SCALING_LIST_SIZE_NUM];
extern UInt g_scalingListSizeX[SCALING_LIST_SIZE_NUM];
extern UInt g_scalingListNum  [SCALING_LIST_SIZE_NUM];
//! \}

#endif  //__TCOMROM__

