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

/** \file     TEncSearch.cpp
 \brief    encoder search class
 */

#include "TLibCommon/TypeDef.h"
#include "TLibCommon/TComRom.h"
#include "TLibCommon/TComMotionInfo.h"
#include "TEncSearch.h"
#include "TLibCommon/TComTU.h"
#include "TLibCommon/Debug.h"
#include <math.h>
#include <limits>


//! \ingroup TLibEncoder
//! \{

static const TComMv s_acMvRefineH[9] =
{
  TComMv(  0,  0 ), // 0
  TComMv(  0, -1 ), // 1
  TComMv(  0,  1 ), // 2
  TComMv( -1,  0 ), // 3
  TComMv(  1,  0 ), // 4
  TComMv( -1, -1 ), // 5
  TComMv(  1, -1 ), // 6
  TComMv( -1,  1 ), // 7
  TComMv(  1,  1 )  // 8
};

static const TComMv s_acMvRefineQ[9] =
{
  TComMv(  0,  0 ), // 0
  TComMv(  0, -1 ), // 1
  TComMv(  0,  1 ), // 2
  TComMv( -1, -1 ), // 5
  TComMv(  1, -1 ), // 6
  TComMv( -1,  0 ), // 3
  TComMv(  1,  0 ), // 4
  TComMv( -1,  1 ), // 7
  TComMv(  1,  1 )  // 8
};

static const UInt s_auiDFilter[9] =
{
  0, 1, 0,
  2, 3, 2,
  0, 1, 0
};

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
static Void offsetSubTUCBFs(TComTU &rTu, const ComponentID compID)
{
        TComDataCU *pcCU              = rTu.getCU();
  const UInt        uiTrDepth         = rTu.GetTransformDepthRel();
  const UInt        uiAbsPartIdx      = rTu.GetAbsPartIdxTU(compID);
  const UInt        partIdxesPerSubTU = rTu.GetAbsPartIdxNumParts(compID) >> 1;

  //move the CBFs down a level and set the parent CBF

  UChar subTUCBF[2];
  UChar combinedSubTUCBF = 0;

  for (UInt subTU = 0; subTU < 2; subTU++)
  {
    const UInt subTUAbsPartIdx = uiAbsPartIdx + (subTU * partIdxesPerSubTU);

    subTUCBF[subTU]   = pcCU->getCbf(subTUAbsPartIdx, compID, uiTrDepth);
    combinedSubTUCBF |= subTUCBF[subTU];
  }

  for (UInt subTU = 0; subTU < 2; subTU++)
  {
    const UInt subTUAbsPartIdx = uiAbsPartIdx + (subTU * partIdxesPerSubTU);
    const UChar compositeCBF = (subTUCBF[subTU] << 1) | combinedSubTUCBF;

    pcCU->setCbfPartRange((compositeCBF << uiTrDepth), compID, subTUAbsPartIdx, partIdxesPerSubTU);
  }
}
#endif


TEncSearch::TEncSearch()
{
  for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    m_ppcQTTempCoeff[ch]             = NULL;
    m_pcQTTempCoeff[ch]              = NULL;
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempArlCoeff[ch]          = NULL;
    m_pcQTTempArlCoeff[ch]           = NULL;
#endif
    m_puhQTTempCbf[ch]               = NULL;
    m_pSharedPredTransformSkip[ch]   = NULL;
    m_pcQTTempTUCoeff[ch]            = NULL;
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempTUArlCoeff[ch]        = NULL;
#endif
    m_puhQTTempTransformSkipFlag[ch] = NULL;
  }
  m_puhQTTempTrIdx                   = NULL;
  m_pcQTTempTComYuv                  = NULL;
  m_pcEncCfg                         = NULL;
  m_pcEntropyCoder                   = NULL;
  m_pTempPel                         = NULL;
  setWpScalingDistParam( NULL, -1, REF_PIC_LIST_X );
}




TEncSearch::~TEncSearch()
{
  if ( m_pTempPel )
  {
    delete [] m_pTempPel;
    m_pTempPel = NULL;
  }

  if ( m_pcEncCfg )
  {
    const UInt uiNumLayersAllocated = m_pcEncCfg->getQuadtreeTULog2MaxSize()-m_pcEncCfg->getQuadtreeTULog2MinSize()+1;

    for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
    {
      for (UInt layer = 0; layer < uiNumLayersAllocated; layer++)
      {
        delete[] m_ppcQTTempCoeff[ch][layer];
#if ADAPTIVE_QP_SELECTION
        delete[] m_ppcQTTempArlCoeff[ch][layer];
#endif
      }
      delete[] m_ppcQTTempCoeff[ch];
      delete[] m_pcQTTempCoeff[ch];
      delete[] m_puhQTTempCbf[ch];
#if ADAPTIVE_QP_SELECTION
      delete[] m_ppcQTTempArlCoeff[ch];
      delete[] m_pcQTTempArlCoeff[ch];
#endif
    }

    for( UInt layer = 0; layer < uiNumLayersAllocated; layer++ )
    {
      m_pcQTTempTComYuv[layer].destroy();
    }
  }

  delete[] m_puhQTTempTrIdx;
  delete[] m_pcQTTempTComYuv;

  for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    delete[] m_pSharedPredTransformSkip[ch];
    delete[] m_pcQTTempTUCoeff[ch];
#if ADAPTIVE_QP_SELECTION
    delete[] m_ppcQTTempTUArlCoeff[ch];
#endif
    delete[] m_puhQTTempTransformSkipFlag[ch];
  }
  m_pcQTTempTransformSkipTComYuv.destroy();

  m_tmpYuvPred.destroy();
}




void TEncSearch::init(TEncCfg*      pcEncCfg,
                      TComTrQuant*  pcTrQuant,
                      Int           iSearchRange,
                      Int           bipredSearchRange,
                      Int           iFastSearch,
                      Int           iMaxDeltaQP,
                      TEncEntropy*  pcEntropyCoder,
                      TComRdCost*   pcRdCost,
                      TEncSbac*** pppcRDSbacCoder,
                      TEncSbac*   pcRDGoOnSbacCoder
                      )
{
  m_pcEncCfg             = pcEncCfg;
  m_pcTrQuant            = pcTrQuant;
  m_iSearchRange         = iSearchRange;
  m_bipredSearchRange    = bipredSearchRange;
  m_iFastSearch          = iFastSearch;
  m_iMaxDeltaQP          = iMaxDeltaQP;
  m_pcEntropyCoder       = pcEntropyCoder;
  m_pcRdCost             = pcRdCost;

  m_pppcRDSbacCoder     = pppcRDSbacCoder;
  m_pcRDGoOnSbacCoder   = pcRDGoOnSbacCoder;

  m_bUseSBACRD          = pppcRDSbacCoder ? true : false;

  for (UInt iDir = 0; iDir < MAX_NUM_REF_LIST_ADAPT_SR; iDir++)
  {
    for (UInt iRefIdx = 0; iRefIdx < MAX_IDX_ADAPT_SR; iRefIdx++)
    {
      m_aaiAdaptSR[iDir][iRefIdx] = iSearchRange;
    }
  }

  m_puiDFilter = s_auiDFilter + 4;

  // initialize motion cost
#if !FIX203
  m_pcRdCost->initRateDistortionModel( m_iSearchRange << 2 );
#endif

  for( Int iNum = 0; iNum < AMVP_MAX_NUM_CANDS+1; iNum++)
  {
    for( Int iIdx = 0; iIdx < AMVP_MAX_NUM_CANDS; iIdx++)
    {
      if (iIdx < iNum)
        m_auiMVPIdxCost[iIdx][iNum] = xGetMvpIdxBits(iIdx, iNum);
      else
        m_auiMVPIdxCost[iIdx][iNum] = MAX_INT;
    }
  }

  const ChromaFormat cform=pcEncCfg->getChromaFormatIdc();
  initTempBuff(cform);

  m_pTempPel = new Pel[g_uiMaxCUWidth*g_uiMaxCUHeight];

  const UInt uiNumLayersToAllocate = pcEncCfg->getQuadtreeTULog2MaxSize()-pcEncCfg->getQuadtreeTULog2MinSize()+1;
  const UInt uiNumPartitions = 1<<(g_uiMaxCUDepth<<1);
  for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
  {
    const UInt csx=::getComponentScaleX(ComponentID(ch), cform);
    const UInt csy=::getComponentScaleY(ComponentID(ch), cform);
    m_ppcQTTempCoeff[ch] = new TCoeff* [uiNumLayersToAllocate];
    m_pcQTTempCoeff[ch]   = new TCoeff [(g_uiMaxCUWidth*g_uiMaxCUHeight)>>(csx+csy)   ];
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempArlCoeff[ch]  = new TCoeff*[uiNumLayersToAllocate];
    m_pcQTTempArlCoeff[ch]   = new TCoeff [(g_uiMaxCUWidth*g_uiMaxCUHeight)>>(csx+csy)   ];
#endif
    m_puhQTTempCbf[ch] = new UChar  [uiNumPartitions];

    for (UInt layer = 0; layer < uiNumLayersToAllocate; layer++)
    {
      m_ppcQTTempCoeff[ch][layer] = new TCoeff[(g_uiMaxCUWidth*g_uiMaxCUHeight)>>(csx+csy)];
#if ADAPTIVE_QP_SELECTION
      m_ppcQTTempArlCoeff[ch][layer]  = new TCoeff[(g_uiMaxCUWidth*g_uiMaxCUHeight)>>(csx+csy) ];
#endif
    }
#if RExt__N0288_SPECIFY_TRANSFORM_SKIP_MAXIMUM_SIZE
    m_pSharedPredTransformSkip[ch] = new    Pel[MAX_TU_SIZE*MAX_TU_SIZE];
    m_pcQTTempTUCoeff[ch]          = new TCoeff[MAX_TU_SIZE*MAX_TU_SIZE];
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempTUArlCoeff[ch]      = new TCoeff[MAX_TU_SIZE*MAX_TU_SIZE];
#endif
#else
    m_pSharedPredTransformSkip[ch] = new Pel[MAX_TS_WIDTH*MAX_TS_HEIGHT];
    m_pcQTTempTUCoeff[ch] = new TCoeff[MAX_TS_WIDTH*MAX_TS_HEIGHT];
#if ADAPTIVE_QP_SELECTION
    m_ppcQTTempTUArlCoeff[ch] = new TCoeff[MAX_TS_WIDTH*MAX_TS_HEIGHT];
#endif
#endif
    m_puhQTTempTransformSkipFlag[ch] = new UChar  [uiNumPartitions];
  }
  m_puhQTTempTrIdx   = new UChar  [uiNumPartitions];
  m_pcQTTempTComYuv  = new TComYuv[uiNumLayersToAllocate];
  for( UInt ui = 0; ui < uiNumLayersToAllocate; ++ui )
  {
    m_pcQTTempTComYuv[ui].create( g_uiMaxCUWidth, g_uiMaxCUHeight, pcEncCfg->getChromaFormatIdc() );
  }
  m_pcQTTempTransformSkipTComYuv.create( g_uiMaxCUWidth, g_uiMaxCUHeight, pcEncCfg->getChromaFormatIdc() );
  m_tmpYuvPred.create(MAX_CU_SIZE, MAX_CU_SIZE, pcEncCfg->getChromaFormatIdc());
}

#if FASTME_SMOOTHER_MV
#define FIRSTSEARCHSTOP     1
#else
#define FIRSTSEARCHSTOP     0
#endif

#define TZ_SEARCH_CONFIGURATION                                                                                 \
const Int  iRaster                  = 5;  /* TZ soll von aussen ?ergeben werden */                            \
const Bool bTestOtherPredictedMV    = 0;                                                                      \
const Bool bTestZeroVector          = 1;                                                                      \
const Bool bTestZeroVectorStart     = 0;                                                                      \
const Bool bTestZeroVectorStop      = 0;                                                                      \
const Bool bFirstSearchDiamond      = 1;  /* 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch */        \
const Bool bFirstSearchStop         = FIRSTSEARCHSTOP;                                                        \
const UInt uiFirstSearchRounds      = 3;  /* first search stop X rounds after best match (must be >=1) */     \
const Bool bEnableRasterSearch      = 1;                                                                      \
const Bool bAlwaysRasterSearch      = 0;  /* ===== 1: BETTER but factor 2 slower ===== */                     \
const Bool bRasterRefinementEnable  = 0;  /* enable either raster refinement or star refinement */            \
const Bool bRasterRefinementDiamond = 0;  /* 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch */        \
const Bool bStarRefinementEnable    = 1;  /* enable either star refinement or raster refinement */            \
const Bool bStarRefinementDiamond   = 1;  /* 1 = xTZ8PointDiamondSearch   0 = xTZ8PointSquareSearch */        \
const Bool bStarRefinementStop      = 0;                                                                      \
const UInt uiStarRefinementRounds   = 2;  /* star refinement stop X rounds after best match (must be >=1) */  \




__inline Void TEncSearch::xTZSearchHelp( TComPattern* pcPatternKey, IntTZSearchStruct& rcStruct, const Int iSearchX, const Int iSearchY, const UChar ucPointNr, const UInt uiDistance )
{
  Distortion  uiSad;

  Pel*  piRefSrch;

  piRefSrch = rcStruct.piRefY + iSearchY * rcStruct.iYStride + iSearchX;

  //-- jclee for using the SAD function pointer
  m_pcRdCost->setDistParam( pcPatternKey, piRefSrch, rcStruct.iYStride,  m_cDistParam );

  // fast encoder decision: use subsampled SAD when rows > 8 for integer ME
  if ( m_pcEncCfg->getUseFastEnc() )
  {
    if ( m_cDistParam.iRows > 8 )
    {
      m_cDistParam.iSubShift = 1;
    }
  }

  setDistParamComp(COMPONENT_Y);

  // distortion
  m_cDistParam.bitDepth = g_bitDepth[CHANNEL_TYPE_LUMA];
  uiSad = m_cDistParam.DistFunc( &m_cDistParam );

  // motion cost
  uiSad += m_pcRdCost->getCost( iSearchX, iSearchY );

  if( uiSad < rcStruct.uiBestSad )
  {
    rcStruct.uiBestSad      = uiSad;
    rcStruct.iBestX         = iSearchX;
    rcStruct.iBestY         = iSearchY;
    rcStruct.uiBestDistance = uiDistance;
    rcStruct.uiBestRound    = 0;
    rcStruct.ucPointNr      = ucPointNr;
  }
}




__inline Void TEncSearch::xTZ2PointSearch( TComPattern* pcPatternKey, IntTZSearchStruct& rcStruct, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB )
{
  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 2 point search,                   //   1 2 3
  // check only the 2 untested points  //   4 0 5
  // around the start point            //   6 7 8
  Int iStartX = rcStruct.iBestX;
  Int iStartY = rcStruct.iBestY;
  switch( rcStruct.ucPointNr )
  {
    case 1:
    {
      if ( (iStartX - 1) >= iSrchRngHorLeft )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY, 0, 2 );
      }
      if ( (iStartY - 1) >= iSrchRngVerTop )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY - 1, 0, 2 );
      }
    }
      break;
    case 2:
    {
      if ( (iStartY - 1) >= iSrchRngVerTop )
      {
        if ( (iStartX - 1) >= iSrchRngHorLeft )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY - 1, 0, 2 );
        }
        if ( (iStartX + 1) <= iSrchRngHorRight )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY - 1, 0, 2 );
        }
      }
    }
      break;
    case 3:
    {
      if ( (iStartY - 1) >= iSrchRngVerTop )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY - 1, 0, 2 );
      }
      if ( (iStartX + 1) <= iSrchRngHorRight )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY, 0, 2 );
      }
    }
      break;
    case 4:
    {
      if ( (iStartX - 1) >= iSrchRngHorLeft )
      {
        if ( (iStartY + 1) <= iSrchRngVerBottom )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY + 1, 0, 2 );
        }
        if ( (iStartY - 1) >= iSrchRngVerTop )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY - 1, 0, 2 );
        }
      }
    }
      break;
    case 5:
    {
      if ( (iStartX + 1) <= iSrchRngHorRight )
      {
        if ( (iStartY - 1) >= iSrchRngVerTop )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY - 1, 0, 2 );
        }
        if ( (iStartY + 1) <= iSrchRngVerBottom )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY + 1, 0, 2 );
        }
      }
    }
      break;
    case 6:
    {
      if ( (iStartX - 1) >= iSrchRngHorLeft )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY , 0, 2 );
      }
      if ( (iStartY + 1) <= iSrchRngVerBottom )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY + 1, 0, 2 );
      }
    }
      break;
    case 7:
    {
      if ( (iStartY + 1) <= iSrchRngVerBottom )
      {
        if ( (iStartX - 1) >= iSrchRngHorLeft )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX - 1, iStartY + 1, 0, 2 );
        }
        if ( (iStartX + 1) <= iSrchRngHorRight )
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY + 1, 0, 2 );
        }
      }
    }
      break;
    case 8:
    {
      if ( (iStartX + 1) <= iSrchRngHorRight )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX + 1, iStartY, 0, 2 );
      }
      if ( (iStartY + 1) <= iSrchRngVerBottom )
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iStartY + 1, 0, 2 );
      }
    }
      break;
    default:
    {
      assert( false );
    }
      break;
  } // switch( rcStruct.ucPointNr )
}




__inline Void TEncSearch::xTZ8PointSquareSearch( TComPattern* pcPatternKey, IntTZSearchStruct& rcStruct, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB, const Int iStartX, const Int iStartY, const Int iDist )
{
  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 8 point search,                   //   1 2 3
  // search around the start point     //   4 0 5
  // with the required  distance       //   6 7 8
  assert( iDist != 0 );
  const Int iTop        = iStartY - iDist;
  const Int iBottom     = iStartY + iDist;
  const Int iLeft       = iStartX - iDist;
  const Int iRight      = iStartX + iDist;
  rcStruct.uiBestRound += 1;

  if ( iTop >= iSrchRngVerTop ) // check top
  {
    if ( iLeft >= iSrchRngHorLeft ) // check top left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iTop, 1, iDist );
    }
    // top middle
    xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );

    if ( iRight <= iSrchRngHorRight ) // check top right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iTop, 3, iDist );
    }
  } // check top
  if ( iLeft >= iSrchRngHorLeft ) // check middle left
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 4, iDist );
  }
  if ( iRight <= iSrchRngHorRight ) // check middle right
  {
    xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 5, iDist );
  }
  if ( iBottom <= iSrchRngVerBottom ) // check bottom
  {
    if ( iLeft >= iSrchRngHorLeft ) // check bottom left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iBottom, 6, iDist );
    }
    // check bottom middle
    xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );

    if ( iRight <= iSrchRngHorRight ) // check bottom right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iBottom, 8, iDist );
    }
  } // check bottom
}




__inline Void TEncSearch::xTZ8PointDiamondSearch( TComPattern* pcPatternKey, IntTZSearchStruct& rcStruct, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB, const Int iStartX, const Int iStartY, const Int iDist )
{
  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  // 8 point search,                   //   1 2 3
  // search around the start point     //   4 0 5
  // with the required  distance       //   6 7 8
  assert ( iDist != 0 );
  const Int iTop        = iStartY - iDist;
  const Int iBottom     = iStartY + iDist;
  const Int iLeft       = iStartX - iDist;
  const Int iRight      = iStartX + iDist;
  rcStruct.uiBestRound += 1;

  if ( iDist == 1 ) // iDist == 1
  {
    if ( iTop >= iSrchRngVerTop ) // check top
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );
    }
    if ( iLeft >= iSrchRngHorLeft ) // check middle left
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 4, iDist );
    }
    if ( iRight <= iSrchRngHorRight ) // check middle right
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 5, iDist );
    }
    if ( iBottom <= iSrchRngVerBottom ) // check bottom
    {
      xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );
    }
  }
  else // if (iDist != 1)
  {
    if ( iDist <= 8 )
    {
      const Int iTop_2      = iStartY - (iDist>>1);
      const Int iBottom_2   = iStartY + (iDist>>1);
      const Int iLeft_2     = iStartX - (iDist>>1);
      const Int iRight_2    = iStartX + (iDist>>1);

      if (  iTop >= iSrchRngVerTop && iLeft >= iSrchRngHorLeft &&
          iRight <= iSrchRngHorRight && iBottom <= iSrchRngVerBottom ) // check border
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX,  iTop,      2, iDist    );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2,  iTop_2,    1, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iTop_2,    3, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft,    iStartY,   4, iDist    );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight,   iStartY,   5, iDist    );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2,  iBottom_2, 6, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iBottom_2, 8, iDist>>1 );
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX,  iBottom,   7, iDist    );
      }
      else // check border
      {
        if ( iTop >= iSrchRngVerTop ) // check top
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 2, iDist );
        }
        if ( iTop_2 >= iSrchRngVerTop ) // check half top
        {
          if ( iLeft_2 >= iSrchRngHorLeft ) // check half left
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2, iTop_2, 1, (iDist>>1) );
          }
          if ( iRight_2 <= iSrchRngHorRight ) // check half right
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iTop_2, 3, (iDist>>1) );
          }
        } // check half top
        if ( iLeft >= iSrchRngHorLeft ) // check left
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 4, iDist );
        }
        if ( iRight <= iSrchRngHorRight ) // check right
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 5, iDist );
        }
        if ( iBottom_2 <= iSrchRngVerBottom ) // check half bottom
        {
          if ( iLeft_2 >= iSrchRngHorLeft ) // check half left
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iLeft_2, iBottom_2, 6, (iDist>>1) );
          }
          if ( iRight_2 <= iSrchRngHorRight ) // check half right
          {
            xTZSearchHelp( pcPatternKey, rcStruct, iRight_2, iBottom_2, 8, (iDist>>1) );
          }
        } // check half bottom
        if ( iBottom <= iSrchRngVerBottom ) // check bottom
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 7, iDist );
        }
      } // check border
    }
    else // iDist > 8
    {
      if ( iTop >= iSrchRngVerTop && iLeft >= iSrchRngHorLeft &&
          iRight <= iSrchRngHorRight && iBottom <= iSrchRngVerBottom ) // check border
      {
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop,    0, iDist );
        xTZSearchHelp( pcPatternKey, rcStruct, iLeft,   iStartY, 0, iDist );
        xTZSearchHelp( pcPatternKey, rcStruct, iRight,  iStartY, 0, iDist );
        xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 0, iDist );
        for ( Int index = 1; index < 4; index++ )
        {
          Int iPosYT = iTop    + ((iDist>>2) * index);
          Int iPosYB = iBottom - ((iDist>>2) * index);
          Int iPosXL = iStartX - ((iDist>>2) * index);
          Int iPosXR = iStartX + ((iDist>>2) * index);
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYT, 0, iDist );
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYT, 0, iDist );
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYB, 0, iDist );
          xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYB, 0, iDist );
        }
      }
      else // check border
      {
        if ( iTop >= iSrchRngVerTop ) // check top
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iTop, 0, iDist );
        }
        if ( iLeft >= iSrchRngHorLeft ) // check left
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iLeft, iStartY, 0, iDist );
        }
        if ( iRight <= iSrchRngHorRight ) // check right
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iRight, iStartY, 0, iDist );
        }
        if ( iBottom <= iSrchRngVerBottom ) // check bottom
        {
          xTZSearchHelp( pcPatternKey, rcStruct, iStartX, iBottom, 0, iDist );
        }
        for ( Int index = 1; index < 4; index++ )
        {
          Int iPosYT = iTop    + ((iDist>>2) * index);
          Int iPosYB = iBottom - ((iDist>>2) * index);
          Int iPosXL = iStartX - ((iDist>>2) * index);
          Int iPosXR = iStartX + ((iDist>>2) * index);

          if ( iPosYT >= iSrchRngVerTop ) // check top
          {
            if ( iPosXL >= iSrchRngHorLeft ) // check left
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYT, 0, iDist );
            }
            if ( iPosXR <= iSrchRngHorRight ) // check right
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYT, 0, iDist );
            }
          } // check top
          if ( iPosYB <= iSrchRngVerBottom ) // check bottom
          {
            if ( iPosXL >= iSrchRngHorLeft ) // check left
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXL, iPosYB, 0, iDist );
            }
            if ( iPosXR <= iSrchRngHorRight ) // check right
            {
              xTZSearchHelp( pcPatternKey, rcStruct, iPosXR, iPosYB, 0, iDist );
            }
          } // check bottom
        } // for ...
      } // check border
    } // iDist <= 8
  } // iDist == 1
}





//<--

Distortion TEncSearch::xPatternRefinement( TComPattern* pcPatternKey,
                                           TComMv baseRefMv,
                                           Int iFrac, TComMv& rcMvFrac )
{
  Distortion  uiDist;
  Distortion  uiDistBest  = std::numeric_limits<Distortion>::max();
  UInt        uiDirecBest = 0;

  Pel*  piRefPos;
  Int iRefStride = m_filteredBlock[0][0].getStride(COMPONENT_Y);
#if NS_HAD
  m_pcRdCost->setDistParam( pcPatternKey, m_filteredBlock[0][0].getAddr(COMPONENT_Y), iRefStride, 1, m_cDistParam, m_pcEncCfg->getUseHADME(), m_pcEncCfg->getUseNSQT() );
#else
  m_pcRdCost->setDistParam( pcPatternKey, m_filteredBlock[0][0].getAddr(COMPONENT_Y), iRefStride, 1, m_cDistParam, m_pcEncCfg->getUseHADME() );
#endif

  const TComMv* pcMvRefine = (iFrac == 2 ? s_acMvRefineH : s_acMvRefineQ);

  for (UInt i = 0; i < 9; i++)
  {
    TComMv cMvTest = pcMvRefine[i];
    cMvTest += baseRefMv;

    Int horVal = cMvTest.getHor() * iFrac;
    Int verVal = cMvTest.getVer() * iFrac;
    piRefPos = m_filteredBlock[ verVal & 3 ][ horVal & 3 ].getAddr(COMPONENT_Y);
    if ( horVal == 2 && ( verVal & 1 ) == 0 )
      piRefPos += 1;
    if ( ( horVal & 1 ) == 0 && verVal == 2 )
      piRefPos += iRefStride;
    cMvTest = pcMvRefine[i];
    cMvTest += rcMvFrac;

    setDistParamComp(COMPONENT_Y);

    m_cDistParam.pCur = piRefPos;
    m_cDistParam.bitDepth = g_bitDepth[CHANNEL_TYPE_LUMA];
    uiDist = m_cDistParam.DistFunc( &m_cDistParam );
    uiDist += m_pcRdCost->getCost( cMvTest.getHor(), cMvTest.getVer() );

    if ( uiDist < uiDistBest )
    {
      uiDistBest  = uiDist;
      uiDirecBest = i;
    }
  }

  rcMvFrac = pcMvRefine[uiDirecBest];

  return uiDistBest;
}



Void
TEncSearch::xEncSubdivCbfQT(TComTU      &rTu,
                            Bool         bLuma,
                            Bool         bChroma )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx         = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth            = rTu.GetTransformDepthRel();
  const UInt uiTrMode             = pcCU->getTransformIdx( uiAbsPartIdx );
  const UInt uiSubdiv             = ( uiTrMode > uiTrDepth ? 1 : 0 );
  const UInt uiLog2LumaTrafoSize  = rTu.GetLog2LumaTrSize();

#if RExt__N0256_INTRA_BLOCK_COPY
  if( pcCU->isIntra(0) && pcCU->getPartitionSize(0) == SIZE_NxN && uiTrDepth == 0 )
#else
  if( pcCU->getPredictionMode(0) == MODE_INTRA && pcCU->getPartitionSize(0) == SIZE_NxN && uiTrDepth == 0 )
#endif
  {
    assert( uiSubdiv );
  }
  else if( uiLog2LumaTrafoSize > pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() )
  {
    assert( uiSubdiv );
  }
  else if( uiLog2LumaTrafoSize == pcCU->getSlice()->getSPS()->getQuadtreeTULog2MinSize() )
  {
    assert( !uiSubdiv );
  }
  else if( uiLog2LumaTrafoSize == pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
  {
    assert( !uiSubdiv );
  }
  else
  {
    assert( uiLog2LumaTrafoSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );
    if( bLuma )
    {
      m_pcEntropyCoder->encodeTransformSubdivFlag( uiSubdiv, 5 - uiLog2LumaTrafoSize );
    }
  }

  if ( bChroma )
  {
    const UInt numberValidComponents = getNumberValidComponents(rTu.GetChromaFormat());
    for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      if( rTu.ProcessingAllQuadrants(compID) && (uiTrDepth==0 || pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepth-1 ) ))
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        m_pcEntropyCoder->encodeQtCbf(rTu, compID, (uiSubdiv == 0));
#else
        m_pcEntropyCoder->encodeQtCbf(rTu, compID);
#endif
    }
  }

  if( uiSubdiv )
  {
    TComTURecurse tuRecurse(rTu, false);
    do
    {
      xEncSubdivCbfQT( tuRecurse, bLuma, bChroma );
    } while (tuRecurse.nextSection(rTu));
  }
  else
  {
    //===== Cbfs =====
    if( bLuma )
    {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
      m_pcEntropyCoder->encodeQtCbf( rTu, COMPONENT_Y, true );
#else
      m_pcEntropyCoder->encodeQtCbf( rTu, COMPONENT_Y );
#endif
    }
  }
}




Void
TEncSearch::xEncCoeffQT(TComTU &rTu,
                        const ComponentID  component,
                        Bool         bRealCoeff )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth=rTu.GetTransformDepthRel();

  const UInt  uiTrMode        = pcCU->getTransformIdx( uiAbsPartIdx );
  const UInt  uiSubdiv        = ( uiTrMode > uiTrDepth ? 1 : 0 );

  if( uiSubdiv )
  {
    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xEncCoeffQT( tuRecurseChild, component, bRealCoeff );
    } while (tuRecurseChild.nextSection(rTu) );
  }
  else if (rTu.ProcessComponentSection(component))
  {
    //===== coefficients =====
    const UInt  uiLog2TrafoSize = rTu.GetLog2LumaTrSize();
    UInt    uiCoeffOffset   = rTu.getCoefficientOffset(component);
    UInt    uiQTLayer       = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrafoSize;
    TCoeff* pcCoeff         = bRealCoeff ? pcCU->getCoeff(component) : m_ppcQTTempCoeff[component][uiQTLayer];

    m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeff+uiCoeffOffset, component );
  }
}




Void
TEncSearch::xEncIntraHeader( TComDataCU*  pcCU,
                            UInt         uiTrDepth,
                            UInt         uiAbsPartIdx,
                            Bool         bLuma,
                            Bool         bChroma )
{
  if( bLuma )
  {
    // CU header
    if( uiAbsPartIdx == 0 )
    {
      if( !pcCU->getSlice()->isIntra() )
      {
        if (pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
        {
          m_pcEntropyCoder->encodeCUTransquantBypassFlag( pcCU, 0, true );
        }
        m_pcEntropyCoder->encodeSkipFlag( pcCU, 0, true );
        m_pcEntropyCoder->encodePredMode( pcCU, 0, true );
      }

#if RExt__N0256_INTRA_BLOCK_COPY
      if (pcCU->getSlice()->getSPS()->getUseIntraBlockCopy())
      {
        m_pcEntropyCoder->encodeIntraBCFlag ( pcCU, 0, true );
        if ( pcCU->isIntraBC( 0 ) )
        {
          m_pcEntropyCoder->encodeIntraBC( pcCU, 0 );
          return;
        }
      }
#endif

      m_pcEntropyCoder  ->encodePartSize( pcCU, 0, pcCU->getDepth(0), true );

      if (pcCU->isIntra(0) && pcCU->getPartitionSize(0) == SIZE_2Nx2N )
      {
        m_pcEntropyCoder->encodeIPCMInfo( pcCU, 0, true );

        if ( pcCU->getIPCMFlag (0))
        {
          return;
        }
      }
    }
    // luma prediction mode
    if( pcCU->getPartitionSize(0) == SIZE_2Nx2N )
    {
      if (uiAbsPartIdx==0)
      {
        m_pcEntropyCoder->encodeIntraDirModeLuma ( pcCU, 0 );
      }
    }
    else
    {
      UInt uiQNumParts = pcCU->getTotalNumPart() >> 2;
      if (uiTrDepth>0 && (uiAbsPartIdx%uiQNumParts)==0) m_pcEntropyCoder->encodeIntraDirModeLuma ( pcCU, uiAbsPartIdx );
    }
  }

#if RExt__N0256_INTRA_BLOCK_COPY
  if( pcCU->isIntraBC( 0 ) )
  {
    return;
  }
#endif

  if( bChroma )
  {
    if( pcCU->getPartitionSize(0) == SIZE_2Nx2N || !enable4ChromaPUsInIntraNxNCU(pcCU->getPic()->getChromaFormat()))
    {
      if(uiAbsPartIdx==0)
      {
         m_pcEntropyCoder->encodeIntraDirModeChroma ( pcCU, uiAbsPartIdx );
      }
    }
    else
    {
      UInt uiQNumParts = pcCU->getTotalNumPart() >> 2;
      assert(uiTrDepth>0);
      if ((uiAbsPartIdx%uiQNumParts)==0)
      {
        m_pcEntropyCoder->encodeIntraDirModeChroma ( pcCU, uiAbsPartIdx );
      }
    }
  }
}




UInt
TEncSearch::xGetIntraBitsQT(TComTU &rTu,
                            Bool         bLuma,
                            Bool         bChroma,
                            Bool         bRealCoeff /* just for test */ )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth=rTu.GetTransformDepthRel();
  m_pcEntropyCoder->resetBits();
  xEncIntraHeader ( pcCU, uiTrDepth, uiAbsPartIdx, bLuma, bChroma );
  xEncSubdivCbfQT ( rTu, bLuma, bChroma );

  if( bLuma )
  {
    xEncCoeffQT   ( rTu, COMPONENT_Y,      bRealCoeff );
  }
  if( bChroma )
  {
    xEncCoeffQT   ( rTu, COMPONENT_Cb,  bRealCoeff );
    xEncCoeffQT   ( rTu, COMPONENT_Cr,  bRealCoeff );
  }
  UInt   uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();

  return uiBits;
}

UInt TEncSearch::xGetIntraBitsQTChroma(TComTU &rTu,
                                       ComponentID compID,
                                       Bool         bRealCoeff /* just for test */ )
{
  m_pcEntropyCoder->resetBits();
  xEncCoeffQT   ( rTu, compID,  bRealCoeff );
  UInt   uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();
  return uiBits;
}

Void TEncSearch::xIntraCodingTUBlock(       TComYuv*    pcOrgYuv,
                                            TComYuv*    pcPredYuv,
                                            TComYuv*    pcResiYuv,
                                            Distortion& ruiDist,
                                      const ComponentID compID,
                                            TComTU&     rTu
                                      DEBUG_STRING_FN_DECLARE(sDebug)
                                           ,Int         default0Save1Load2
                                     )
{
  if (!rTu.ProcessComponentSection(compID)) return;
  const Bool       bIsLuma = isLuma(compID);
  const TComRectangle &rect= rTu.getRect(compID);
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiAbsPartIdx=rTu.GetAbsPartIdxTU();

  const UInt uiTrDepth=rTu.GetTransformDepthRelAdj(compID);
  const UInt uiFullDepth   = rTu.GetTransformDepthTotal();
  const UInt uiLog2TrSize  = rTu.GetLog2LumaTrSize();
  const ChromaFormat chFmt = pcOrgYuv->getChromaFormat();
  const ChannelType chType = toChannelType(compID);

  const UInt    uiWidth           = rect.width;
  const UInt    uiHeight          = rect.height;
  const UInt    uiStride          = pcOrgYuv ->getStride (compID);
        Pel*    piOrg             = pcOrgYuv ->getAddr( compID, uiAbsPartIdx );
        Pel*    piPred            = pcPredYuv->getAddr( compID, uiAbsPartIdx );
        Pel*    piResi            = pcResiYuv->getAddr( compID, uiAbsPartIdx );
        Pel*    piReco            = pcPredYuv->getAddr( compID, uiAbsPartIdx );
  const UInt    uiQTLayer           = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
        Pel*    piRecQt           = m_pcQTTempTComYuv[ uiQTLayer ].getAddr( compID, uiAbsPartIdx );
  const UInt    uiRecQtStride     = m_pcQTTempTComYuv[ uiQTLayer ].getStride(compID);
  const UInt    uiZOrder            = pcCU->getZorderIdxInCU() + uiAbsPartIdx;
        Pel*    piRecIPred        = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getAddr(), uiZOrder );
        UInt    uiRecIPredStride  = pcCU->getPic()->getPicYuvRec()->getStride  ( compID );
        TCoeff* pcCoeff           = m_ppcQTTempCoeff[compID][uiQTLayer] + rTu.getCoefficientOffset(compID);
        Bool    useTransformSkip  = pcCU->getTransformSkip(uiAbsPartIdx, compID);

#if ADAPTIVE_QP_SELECTION
        TCoeff*    pcArlCoeff     = m_ppcQTTempArlCoeff[compID][ uiQTLayer ] + rTu.getCoefficientOffset(compID);
#endif

  const UInt uiChPredMode  = pcCU->getIntraDir( chType, uiAbsPartIdx );
  const UInt uiChCodedMode = (uiChPredMode==DM_CHROMA_IDX && !bIsLuma) ? pcCU->getIntraDir(CHANNEL_TYPE_LUMA, getChromasCorrespondingPULumaIdx(uiAbsPartIdx, chFmt)) : uiChPredMode;
  const UInt uiChFinalMode = ((chFmt == CHROMA_422)       && !bIsLuma) ? g_chroma422IntraAngleMappingTable[uiChCodedMode] : uiChCodedMode;

  //===== init availability pattern =====
  Bool  bAboveAvail = false;
  Bool  bLeftAvail  = false;

  DEBUG_STRING_NEW(sTemp)

#ifndef DEBUG_STRING
  if( default0Save1Load2 != 2 )
#endif
  {
#if RExt__N0080_INTRA_REFERENCE_SMOOTHING_DISABLED_FLAG
    const Bool bUseFilteredPredictions=TComPrediction::filteringIntraReferenceSamples(compID, uiChFinalMode, uiWidth, uiHeight, chFmt, pcCU->getSlice()->getSPS()->getDisableIntraReferenceSmoothing());
#else
    const Bool bUseFilteredPredictions=TComPrediction::filteringIntraReferenceSamples(compID, uiChFinalMode, uiWidth, uiHeight, chFmt);
#endif

    initAdiPatternChType( rTu, bAboveAvail, bLeftAvail, compID, bUseFilteredPredictions DEBUG_STRING_PASS_INTO(sDebug) );

    //===== get prediction signal =====
    predIntraAng( compID, uiChFinalMode, piOrg, uiStride, piPred, uiStride, rTu, bAboveAvail, bLeftAvail, bUseFilteredPredictions );

    // save prediction
    if( default0Save1Load2 == 1 )
    {
      Pel*  pPred   = piPred;
      Pel*  pPredBuf = m_pSharedPredTransformSkip[compID];
      Int k = 0;
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pPredBuf[ k ++ ] = pPred[ uiX ];
        }
        pPred += uiStride;
      }
    }
  }
#ifndef DEBUG_STRING
  else
  {
    // load prediction
    Pel*  pPred   = piPred;
    Pel*  pPredBuf = m_pSharedPredTransformSkip[compID];
    Int k = 0;
    for( UInt uiY = 0; uiY < uiHeight; uiY++ )
    {
      for( UInt uiX = 0; uiX < uiWidth; uiX++ )
      {
        pPred[ uiX ] = pPredBuf[ k ++ ];
      }
      pPred += uiStride;
    }
  }
#endif

#if RExt__NRCE2_RESIDUAL_DPCM
  if ( !pcCU->getCUTransquantBypass(uiAbsPartIdx) && useTransformSkip && pcCU->isIntra(uiAbsPartIdx) && ( (uiChFinalMode == HOR_IDX) || (uiChFinalMode == VER_IDX) ) && pcCU->isRDPCMEnabled(uiAbsPartIdx) )
  {
    const UInt uiOrgTrDepth   = rTu.GetTransformDepthRel();

    // quantization params
    UInt uiAbsSum = 0;
    if (bIsLuma)
    {
      pcCU->setTrIdxSubParts ( uiTrDepth, uiAbsPartIdx, uiFullDepth );
    }

    QpParam cQP;
    setQPforQuant   ( cQP, pcCU->getQP( 0 ), chType, pcCU->getSlice()->getSPS()->getQpBDOffset(chType), pcCU->getSlice()->getPPS()->getQpOffset(compID)+ pcCU->getSlice()->getSliceChromaQpDelta(compID), chFmt, useTransformSkip );

    Int    tmpResi   [ MAX_CU_SIZE*MAX_CU_SIZE ];
#if defined DEBUG_STRING
    TCoeff tmpTransformedDequantised  [ MAX_CU_SIZE*MAX_CU_SIZE ];
#endif
    Int    resiDiff;

#if RExt__NRCE2_RESIDUAL_ROTATION
    const Bool rotateResidual = pcCU->isResidualRotated(uiWidth);
    const UInt lastColumn     = uiWidth  - 1;
    const UInt lastRow        = uiHeight - 1;
#endif

    UInt uiPos;
    if ( uiChFinalMode == VER_IDX )
    {

      for ( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for ( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
#if RExt__NRCE2_RESIDUAL_ROTATION
          uiPos = (rotateResidual ? (((lastRow - uiY) * uiWidth) + (lastColumn - uiX)) : ((uiY * uiWidth) + uiX));
#else
          uiPos = uiY*uiWidth + uiX;
#endif

          tmpResi[ uiY*uiWidth+uiX ] = piOrg[ uiY*uiStride+uiX ] - piPred[ uiY*uiStride+uiX ];

          if ( uiY > 0 )
          {
            resiDiff = tmpResi[ uiY*uiWidth+uiX ] - tmpResi[ (uiY-1)*uiWidth+uiX ];
          }
          else
          {
            resiDiff = tmpResi[ uiY*uiWidth+uiX ];
          }

          m_pcTrQuant->transformSkipQuantOneSample(rTu, compID, resiDiff, pcCoeff, uiPos, cQP);

          TCoeff deQuantSample;
          m_pcTrQuant->invTrSkipDeQuantOneSample(rTu, compID, pcCoeff[uiPos], deQuantSample, cQP, uiPos DEBUG_STRING_PASS_INTO(tmpTransformedDequantised[ uiPos ]));
          if (deQuantSample != 0)
          {
            uiAbsSum++;
          }

          piResi[ uiY*uiStride + uiX ] = Pel(deQuantSample);

          if ( uiY > 0 )
          {
            tmpResi[ uiY*uiWidth + uiX] = piResi[ uiY*uiStride + uiX ] + tmpResi[ (uiY-1)*uiWidth + uiX];
          }
          else
          {
            tmpResi[ uiY*uiWidth + uiX] = piResi[ uiY*uiStride + uiX ];
          }
        }
      }
    }
    else if ( uiChFinalMode == HOR_IDX )
    {
      for ( UInt uiX = 0; uiX < uiWidth; uiX++ )
      {
        for ( UInt uiY = 0; uiY < uiHeight; uiY++ )
        {
#if RExt__NRCE2_RESIDUAL_ROTATION
          uiPos = (rotateResidual ? (((lastRow - uiY) * uiWidth) + (lastColumn - uiX)) : ((uiY * uiWidth) + uiX));
#else
          uiPos = uiY*uiWidth + uiX;
#endif
          tmpResi[ uiY*uiWidth+uiX ] = piOrg[ uiY*uiStride+uiX ] - piPred[ uiY*uiStride+uiX ];

          if ( uiX > 0 )
          {
            resiDiff = tmpResi[ uiY*uiWidth+uiX ] - tmpResi[ uiY*uiWidth+uiX-1 ];
          }
          else
          {
            resiDiff = tmpResi[ uiY*uiWidth+uiX ];
          }

          m_pcTrQuant->transformSkipQuantOneSample(rTu, compID, resiDiff, pcCoeff, uiPos, cQP);

          TCoeff deQuantSample;
          m_pcTrQuant->invTrSkipDeQuantOneSample(rTu, compID, pcCoeff[uiPos], deQuantSample, cQP, uiPos DEBUG_STRING_PASS_INTO(tmpTransformedDequantised[ uiPos ]));

          if (deQuantSample != 0)
          {
            uiAbsSum++;
          }

          piResi[ uiY*uiStride + uiX ] = Pel(deQuantSample);
          if ( uiX > 0 )
          {
            tmpResi[ uiY*uiWidth + uiX] = piResi[ uiY*uiStride + uiX ] + tmpResi[ uiY*uiWidth+uiX-1 ];
          }
          else
          {
            tmpResi[ uiY*uiWidth + uiX] = piResi[ uiY*uiStride + uiX ];
          }
        }
      }
    }

#if defined DEBUG_STRING
    {
      std::stringstream ss(stringstream::out);
      printBlockToStream(ss, (compID==0)?"###InvTran ip Ch0: " : ((compID==1)?"###InvTran ip Ch1: ":"###InvTran ip Ch2: "), pcCoeff, uiWidth, uiHeight, uiWidth);
      printBlockToStream(ss, "###InvTran deq: ",  tmpTransformedDequantised, uiWidth, uiHeight, uiWidth);
      printBlockToStream(ss, "###InvTran resi: ", piResi, uiWidth, uiHeight, uiStride);
      sDebug+=ss.str();
      sDebug+="(TS)\n";
    }
#endif


  //set the CBF
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
    pcCU->setCbfPartRange((((uiAbsSum > 0) ? 1 : 0) << uiOrgTrDepth), compID, uiAbsPartIdx, rTu.GetAbsPartIdxNumParts(compID));
#else
    pcCU->setCbfSubParts((((uiAbsSum > 0) ? 1 : 0) << uiOrgTrDepth), compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID));
#endif

  }
  else
  {
#endif // RExt__NRCE2_RESIDUAL_DPCM

    //===== get residual signal =====
    {
      // get residual
      Pel*  pOrg    = piOrg;
      Pel*  pPred   = piPred;
      Pel*  pResi   = piResi;

      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pResi[ uiX ] = pOrg[ uiX ] - pPred[ uiX ];
        }

        pOrg  += uiStride;
        pResi += uiStride;
        pPred += uiStride;
      }
    }


    //===== transform and quantization =====
    //--- init rate estimation arrays for RDOQ ---
    if( useTransformSkip ? m_pcEncCfg->getUseRDOQTS() : m_pcEncCfg->getUseRDOQ() )
    {
      m_pcEntropyCoder->estimateBit( m_pcTrQuant->m_pcEstBitsSbac, uiWidth, uiHeight, chType );
    }

    //--- transform and quantization ---
    TCoeff uiAbsSum = 0;
    if (bIsLuma)
    {
      pcCU       ->setTrIdxSubParts ( uiTrDepth, uiAbsPartIdx, uiFullDepth );
    }

    QpParam cQP;
    setQPforQuant   ( cQP, pcCU->getQP( 0 ), chType, pcCU->getSlice()->getSPS()->getQpBDOffset(chType), pcCU->getSlice()->getPPS()->getQpOffset(compID)+ pcCU->getSlice()->getSliceChromaQpDelta(compID), chFmt, useTransformSkip );

#if RDOQ_CHROMA_LAMBDA
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_990
    m_pcTrQuant->selectLambda     (chType);
#else
    m_pcTrQuant->selectLambda     (compID);
#endif
#endif

    m_pcTrQuant->transformNxN     ( rTu, compID, piResi, uiStride, pcCoeff,
#if ADAPTIVE_QP_SELECTION
      pcArlCoeff,
#endif
      uiAbsSum, cQP
      );

    //--- inverse transform ---

    if ( uiAbsSum > 0 )
    {
#if DEBUG_INTRA_CODING_INV_TRAN
      m_pcTrQuant->invTransformNxN ( rTu, compID, piResi, uiStride, pcCoeff, cQP DEBUG_STRING_PASS_INTO(&sDebug) );
#else
      m_pcTrQuant->invTransformNxN ( rTu, compID, piResi, uiStride, pcCoeff, cQP DEBUG_STRING_PASS_INTO(0) );
#endif
    }
    else
    {
      Pel* pResi = piResi;
      memset( pcCoeff, 0, sizeof( TCoeff ) * uiWidth * uiHeight );
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        memset( pResi, 0, sizeof( Pel ) * uiWidth );
        pResi += uiStride;
      }
    }

#if RExt__NRCE2_RESIDUAL_DPCM
  }
#endif

  //===== reconstruction =====
  {
    Pel* pPred      = piPred;
    Pel* pResi      = piResi;
    Pel* pReco      = piReco;
    Pel* pRecQt     = piRecQt;
    Pel* pRecIPred  = piRecIPred;
    const UInt clipbd=g_bitDepth[chType];

#if RExt__NRCE2_RESIDUAL_DPCM
    if ( !pcCU->getCUTransquantBypass(uiAbsPartIdx) && useTransformSkip && pcCU->isIntra(uiAbsPartIdx) && (uiChFinalMode == VER_IDX) && pcCU->isRDPCMEnabled(uiAbsPartIdx))
    {
      pResi += uiStride;
      for( UInt uiY = 1; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pResi[ uiX ] = pResi[ uiX ] + pResi [ (Int)uiX - (Int)uiStride ];
        }
        pResi += uiStride;
      }
    }

    if ( !pcCU->getCUTransquantBypass(uiAbsPartIdx) && useTransformSkip && pcCU->isIntra(uiAbsPartIdx) && (uiChFinalMode == HOR_IDX) && pcCU->isRDPCMEnabled(uiAbsPartIdx))
    {
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 1; uiX < uiWidth; uiX++ )
        {
          pResi[ uiX ] = pResi[ uiX ] + pResi [ (Int)uiX-1 ];
        }
        pResi     += uiStride;
      }
    }
    pResi = piResi;
#endif


 #if defined DEBUG_STRING && DEBUG_INTRA_CODING_TU
    std::stringstream ss(stringstream::out);

    if (DEBUG_STRING_CHANNEL_CONDITION(compID))
    {
      ss << "###: " << "CompID: " << compID << " pred mode (ch/fin): " << uiChPredMode << "/" << uiChFinalMode << " absPartIdx: " << rTu.GetAbsPartIdxTU() << std::endl;
      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        ss << "###: ";
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          ss << pPred[ uiX ] << ", ";
        }
        ss << "  --  ";
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          ss << pResi[ uiX ] << ", ";
          pReco    [ uiX ] = Pel(ClipBD<Int>( Int(pPred[uiX]) + Int(pResi[uiX]), clipbd ));
          pRecQt   [ uiX ] = pReco[ uiX ];
          pRecIPred[ uiX ] = pReco[ uiX ];
        }
        pPred     += uiStride;
        pResi     += uiStride;
        pReco     += uiStride;
        pRecQt    += uiRecQtStride;
        pRecIPred += uiRecIPredStride;
        ss << "\n";
      }
      DEBUG_STRING_APPEND(sDebug, ss.str())
    }
    else
#endif
    {

      for( UInt uiY = 0; uiY < uiHeight; uiY++ )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          pReco    [ uiX ] = Pel(ClipBD<Int>( Int(pPred[uiX]) + Int(pResi[uiX]), clipbd ));
          pRecQt   [ uiX ] = pReco[ uiX ];
          pRecIPred[ uiX ] = pReco[ uiX ];
        }
        pPred     += uiStride;
        pResi     += uiStride;
        pReco     += uiStride;
        pRecQt    += uiRecQtStride;
        pRecIPred += uiRecIPredStride;
      }
    }
  }

  //===== update distortion =====
#if WEIGHTED_CHROMA_DISTORTION
  ruiDist += m_pcRdCost->getDistPart( g_bitDepth[chType], piReco, uiStride, piOrg, uiStride, uiWidth, uiHeight, compID );
#else
  ruiDist += m_pcRdCost->getDistPart( g_bitDepth[chType], piReco, uiStride, piOrg, uiStride, uiWidth, uiHeight );
#endif
}





Void
TEncSearch::xRecurIntraCodingQT(Bool        bLumaOnly,
                                TComYuv*    pcOrgYuv,
                                TComYuv*    pcPredYuv,
                                TComYuv*    pcResiYuv,
                                Distortion& ruiDistY,
                                Distortion& ruiDistC,
#if HHI_RQT_INTRA_SPEEDUP
                                Bool        bCheckFirst,
#endif
                                Double&     dRDCost,
                                TComTU&     rTu
                                DEBUG_STRING_FN_DECLARE(sDebug))
{
  TComDataCU   *pcCU          = rTu.getCU();
  const UInt    uiAbsPartIdx  = rTu.GetAbsPartIdxTU();
  const UInt    uiFullDepth   = rTu.GetTransformDepthTotal();
  const UInt    uiTrDepth     = rTu.GetTransformDepthRel();
  const UInt    uiLog2TrSize  = rTu.GetLog2LumaTrSize();
        Bool    bCheckFull    = ( uiLog2TrSize  <= pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() );
        Bool    bCheckSplit   = ( uiLog2TrSize  >  pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );
  const UInt    numValidComp  = (bLumaOnly) ? 1 : pcOrgYuv->getNumberValidComponents();

#if HHI_RQT_INTRA_SPEEDUP
  Int maxTuSize = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize();
  Int isIntraSlice = (pcCU->getSlice()->getSliceType() == I_SLICE);
  // don't check split if TU size is less or equal to max TU size
  Bool noSplitIntraMaxTuSize = bCheckFull;
  if(m_pcEncCfg->getRDpenalty() && ! isIntraSlice)
  {
    // in addition don't check split if TU size is less or equal to 16x16 TU size for non-intra slice
    noSplitIntraMaxTuSize = ( uiLog2TrSize  <= min(maxTuSize,4) );

    // if maximum RD-penalty don't check TU size 32x32
    if(m_pcEncCfg->getRDpenalty()==2)
    {
      bCheckFull    = ( uiLog2TrSize  <= min(maxTuSize,4));
    }
  }
  if( bCheckFirst && noSplitIntraMaxTuSize )

  {
    bCheckSplit = false;
  }
#else
  Int maxTuSize = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize();
  Int isIntraSlice = (pcCU->getSlice()->getSliceType() == I_SLICE);
  // if maximum RD-penalty don't check TU size 32x32
  if((m_pcEncCfg->getRDpenalty()==2)  && !isIntraSlice)
  {
    bCheckFull    = ( uiLog2TrSize  <= min(maxTuSize,4));
  }
#endif
  Double     dSingleCost                        = MAX_DOUBLE;
  Distortion uiSingleDist[MAX_NUM_CHANNEL_TYPE] = {0,0};
  UInt       uiSingleCbf[MAX_NUM_COMPONENT]     = {0,0,0};
  Bool       checkTransformSkip  = pcCU->getSlice()->getPPS()->getUseTransformSkip();
  Int        bestModeId[MAX_NUM_COMPONENT] = { 0, 0, 0};
#if RExt__N0288_SPECIFY_TRANSFORM_SKIP_MAXIMUM_SIZE
  checkTransformSkip           &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Y), pcCU->getSlice()->getPPS()->getTransformSkipLog2MaxSize());
#else
  checkTransformSkip           &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Y));
#endif
  checkTransformSkip           &= (!pcCU->getCUTransquantBypass(0));
#if RExt__BACKWARDS_COMPATIBILITY_HM_TRANSQUANTBYPASS
  checkTransformSkip           &= (!((pcCU->getQP( 0 ) == 0) && (pcCU->getSlice()->getSPS()->getUseLossless())));
#endif

  if ( m_pcEncCfg->getUseTransformSkipFast() )
  {
    checkTransformSkip       &= (pcCU->getPartitionSize(uiAbsPartIdx)==SIZE_NxN);
  }

  if( bCheckFull )
  {
    if(checkTransformSkip == true)
    {
      //----- store original entropy coding status -----
      if( m_bUseSBACRD )
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
      }
      Distortion singleDistTmp[MAX_NUM_CHANNEL_TYPE]  = { 0, 0 };
      UInt       singleCbfTmp[MAX_NUM_COMPONENT]      = { 0, 0, 0 };
      Double     singleCostTmp                        = 0;
      Int        firstCheckId                         = 0;

      for(Int modeId = firstCheckId; modeId < 2; modeId ++)
      {
        DEBUG_STRING_NEW(sModeString)
        Int  default0Save1Load2 = 0;
        singleDistTmp[0]=singleDistTmp[1]=0;
        if(modeId == firstCheckId)
        {
          default0Save1Load2 = 1;
        }
        else
        {
          default0Save1Load2 = 2;
        }

        for(UInt ch=COMPONENT_Y; ch<numValidComp; ch++)
        {
          const ComponentID compID = ComponentID(ch);
          if (rTu.ProcessComponentSection(compID))
          {
            const UInt totalAdjustedDepthChan = rTu.GetTransformDepthTotalAdj(compID);
            pcCU->setTransformSkipSubParts ( modeId, compID, uiAbsPartIdx, totalAdjustedDepthChan );
            xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, singleDistTmp[toChannelType(compID)], compID, rTu DEBUG_STRING_PASS_INTO(sModeString), default0Save1Load2 );
          }
          singleCbfTmp[compID] = pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepth );
        }
        //----- determine rate and r-d cost -----
        if(modeId == 1 && singleCbfTmp[COMPONENT_Y] == 0)
        {
          //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
          singleCostTmp = MAX_DOUBLE;
        }
        else
        {
          UInt uiSingleBits = xGetIntraBitsQT( rTu, true, !bLumaOnly, false );
          singleCostTmp     = m_pcRdCost->calcRdCost( uiSingleBits, singleDistTmp[CHANNEL_TYPE_LUMA] + singleDistTmp[CHANNEL_TYPE_CHROMA] );
        }
        if(singleCostTmp < dSingleCost)
        {
          DEBUG_STRING_SWAP(sDebug, sModeString)
          dSingleCost   = singleCostTmp;
          uiSingleDist[CHANNEL_TYPE_LUMA] = singleDistTmp[CHANNEL_TYPE_LUMA];
          uiSingleDist[CHANNEL_TYPE_CHROMA] = singleDistTmp[CHANNEL_TYPE_CHROMA];
          for (UInt ch=0; ch<MAX_NUM_COMPONENT; ch++)
            uiSingleCbf[ch] = singleCbfTmp[ch];

          bestModeId[COMPONENT_Y] = modeId;
          if(bestModeId[COMPONENT_Y] == firstCheckId)
          {
            xStoreIntraResultQT(COMPONENT_Y, bLumaOnly?COMPONENT_Y:COMPONENT_Cr, rTu );
            if( m_bUseSBACRD)
            {
              m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
            }
          }
        }
        if (modeId == firstCheckId)
        {
          m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
        }
      }

      for(UInt ch=COMPONENT_Y; ch<numValidComp; ch++)
      {
        const ComponentID compID=ComponentID(ch);
        if (rTu.ProcessComponentSection(compID))
        {
          const UInt totalAdjustedDepthChan   = rTu.GetTransformDepthTotalAdj(compID);
          pcCU ->setTransformSkipSubParts ( bestModeId[COMPONENT_Y], compID, uiAbsPartIdx, totalAdjustedDepthChan );
        }
      }

      if(bestModeId[COMPONENT_Y] == firstCheckId)
      {
        xLoadIntraResultQT(COMPONENT_Y, bLumaOnly?COMPONENT_Y:COMPONENT_Cr, rTu );
        for(UInt ch=COMPONENT_Y; ch< numValidComp; ch++)
        {
          const ComponentID compID=ComponentID(ch);
          if (rTu.ProcessComponentSection(compID))
            pcCU->setCbfSubParts  ( uiSingleCbf[compID] << uiTrDepth, compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID) );
        }
        if(m_bUseSBACRD)
        {
          m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
        }
      }

      if( !bLumaOnly )
      {
        bestModeId[COMPONENT_Cb] = bestModeId[COMPONENT_Cr] = bestModeId[COMPONENT_Y];
        if (rTu.ProcessComponentSection(COMPONENT_Cb) && bestModeId[COMPONENT_Y] == 1)
        {
          //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
          for (UInt ch=COMPONENT_Cb; ch<numValidComp; ch++)
          {
            if (uiSingleCbf[ch] == 0)
            {
              const ComponentID compID=ComponentID(ch);
              const UInt totalAdjustedDepthChan = rTu.GetTransformDepthTotalAdj(compID);
              pcCU ->setTransformSkipSubParts ( 0, compID, uiAbsPartIdx, totalAdjustedDepthChan);
              bestModeId[ch] = 0;
            }
          }
        }
      }
    }
    else
    {
      //----- store original entropy coding status -----
      if( m_bUseSBACRD && bCheckSplit )
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
      }
      //----- code luma/chroma block with given intra prediction mode and store Cbf-----
      dSingleCost   = 0.0;
      for (UInt ch=COMPONENT_Y; ch<numValidComp; ch++)
      {
        const ComponentID compID = ComponentID(ch);

        if (rTu.ProcessComponentSection(compID))
        {
          const UInt totalAdjustedDepthChan   = rTu.GetTransformDepthTotalAdj(compID);
          pcCU ->setTransformSkipSubParts ( 0, compID, uiAbsPartIdx, totalAdjustedDepthChan );
        }

        xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, uiSingleDist[toChannelType(compID)], compID, rTu DEBUG_STRING_PASS_INTO(sDebug));
        if( bCheckSplit )
        {
          uiSingleCbf[compID] = pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepth );
        }
      }
      //----- determine rate and r-d cost -----
      UInt uiSingleBits = xGetIntraBitsQT( rTu, true, !bLumaOnly, false );

      if(m_pcEncCfg->getRDpenalty() && (uiLog2TrSize==5) && !isIntraSlice)
      {
        uiSingleBits=uiSingleBits*4;
      }

      dSingleCost       = m_pcRdCost->calcRdCost( uiSingleBits, uiSingleDist[CHANNEL_TYPE_LUMA] + uiSingleDist[CHANNEL_TYPE_CHROMA] );
    }
  }

  if( bCheckSplit )
  {
    //----- store full entropy coding status, load original entropy coding status -----
    if( m_bUseSBACRD )
    {
      if( bCheckFull )
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_TEST ] );
        m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
      }
      else
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
      }
    }
    //----- code splitted block -----
    Double     dSplitCost                         = 0.0;
    Distortion uiSplitDist[MAX_NUM_CHANNEL_TYPE]  = {0,0};
    UInt       uiSplitCbf[MAX_NUM_COMPONENT]      = {0,0,0};

    TComTURecurse tuRecurseChild(rTu, false);
    DEBUG_STRING_NEW(sSplit)
    do
    {
      DEBUG_STRING_NEW(sChild)
#if HHI_RQT_INTRA_SPEEDUP
      xRecurIntraCodingQT( bLumaOnly, pcOrgYuv, pcPredYuv, pcResiYuv, uiSplitDist[0], uiSplitDist[1], bCheckFirst, dSplitCost, tuRecurseChild DEBUG_STRING_PASS_INTO(sChild) );
#else
      xRecurIntraCodingQT( bLumaOnly, pcOrgYuv, pcPredYuv, pcResiYuv, uiSplitDist[0], uiSplitDist[1], dSplitCost, tuRecurseChild );
#endif
      DEBUG_STRING_APPEND(sSplit, sChild)
      for(UInt ch=0; ch<numValidComp; ch++)
      {
        uiSplitCbf[ch] |= pcCU->getCbf( tuRecurseChild.GetAbsPartIdxTU(), ComponentID(ch), tuRecurseChild.GetTransformDepthRel() );
      }
    } while (tuRecurseChild.nextSection(rTu) );

    UInt    uiPartsDiv     = rTu.GetAbsPartIdxNumParts();
    for(UInt ch=COMPONENT_Y; ch<numValidComp; ch++)
    {
      if (uiSplitCbf[ch])
      {
        const UInt flag=1<<uiTrDepth;
        const ComponentID compID=ComponentID(ch);
        UChar *pBase=pcCU->getCbf( compID );
        for( UInt uiOffs = 0; uiOffs < uiPartsDiv; uiOffs++ )
        {
          pBase[ uiAbsPartIdx + uiOffs ] |= flag;
        }
      }
    }
    //----- restore context states -----
    if( m_bUseSBACRD )
    {
      m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
    }
    //----- determine rate and r-d cost -----
    UInt uiSplitBits = xGetIntraBitsQT( rTu, true, !bLumaOnly, false );
    dSplitCost       = m_pcRdCost->calcRdCost( uiSplitBits, uiSplitDist[CHANNEL_TYPE_LUMA] + uiSplitDist[CHANNEL_TYPE_CHROMA] );

    //===== compare and set best =====
    if( dSplitCost < dSingleCost )
    {
      //--- update cost ---
      DEBUG_STRING_SWAP(sSplit, sDebug)
      ruiDistY += uiSplitDist[CHANNEL_TYPE_LUMA];
      ruiDistC += uiSplitDist[CHANNEL_TYPE_CHROMA];
      dRDCost  += dSplitCost;
      return;
    }

    //----- set entropy coding status -----
    if( m_bUseSBACRD )
    {
      m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_TEST ] );
    }

    //--- set transform index and Cbf values ---
    pcCU->setTrIdxSubParts( uiTrDepth, uiAbsPartIdx, uiFullDepth );
    for(UInt ch=0; ch<numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      const TComRectangle &tuRect=rTu.getRect(compID);
      const UInt totalAdjustedDepthChan   = rTu.GetTransformDepthTotalAdj(compID);
      pcCU->setCbfSubParts  ( uiSingleCbf[compID] << uiTrDepth, compID, uiAbsPartIdx, totalAdjustedDepthChan );
      pcCU ->setTransformSkipSubParts  ( bestModeId[compID], compID, uiAbsPartIdx, totalAdjustedDepthChan );

      //--- set reconstruction for next intra prediction blocks ---
      const UInt  uiQTLayer   = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
      const UInt  uiZOrder    = pcCU->getZorderIdxInCU() + uiAbsPartIdx;
      const UInt  uiWidth     = tuRect.width;
      const UInt  uiHeight    = tuRect.height;
      Pel*  piSrc       = m_pcQTTempTComYuv[ uiQTLayer ].getAddr( compID, uiAbsPartIdx );
      UInt  uiSrcStride = m_pcQTTempTComYuv[ uiQTLayer ].getStride  ( compID );
      Pel*  piDes       = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getAddr(), uiZOrder );
      UInt  uiDesStride = pcCU->getPic()->getPicYuvRec()->getStride  ( compID );

      for( UInt uiY = 0; uiY < uiHeight; uiY++, piSrc += uiSrcStride, piDes += uiDesStride )
      {
        for( UInt uiX = 0; uiX < uiWidth; uiX++ )
        {
          piDes[ uiX ] = piSrc[ uiX ];
        }
      }
    }
  }
  ruiDistY += uiSingleDist[CHANNEL_TYPE_LUMA];
  ruiDistC += uiSingleDist[CHANNEL_TYPE_CHROMA];
  dRDCost  += dSingleCost;
}


Void
TEncSearch::xSetIntraResultQT(Bool        bLumaOnly,
                              TComYuv*    pcRecoYuv,
                              TComTU     &rTu)
{
  TComDataCU *pcCU        = rTu.getCU();
  const UInt uiTrDepth    = rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if(  uiTrMode == uiTrDepth )
  {
    UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    Bool bSkipChroma = !rTu.ProcessChannelSection(CHANNEL_TYPE_CHROMA);

    //===== copy transform coefficients =====

    const UInt numChannelsToProcess = (bLumaOnly || bSkipChroma) ? 1 : ::getNumberValidComponents(pcCU->getPic()->getChromaFormat());
    for (UInt ch=0; ch<numChannelsToProcess; ch++)
    {
      const ComponentID compID = ComponentID(ch);
      const TComRectangle &tuRect=rTu.getRect(compID);
      const UInt coeffOffset = rTu.getCoefficientOffset(compID);
      const UInt numCoeffInBlock = tuRect.width * tuRect.height;

      if (numCoeffInBlock!=0)
      {
        const TCoeff* srcCoeff = m_ppcQTTempCoeff[compID][uiQTLayer] + coeffOffset;
        TCoeff* destCoeff      = pcCU->getCoeff(compID) + coeffOffset;
        ::memcpy( destCoeff, srcCoeff, sizeof(TCoeff)*numCoeffInBlock );
#if ADAPTIVE_QP_SELECTION
        const TCoeff* srcArlCoeff = m_ppcQTTempArlCoeff[compID][ uiQTLayer ] + coeffOffset;
        TCoeff* destArlCoeff      = pcCU->getArlCoeff (compID)               + coeffOffset;
        ::memcpy( destArlCoeff, srcArlCoeff, sizeof( TCoeff ) * numCoeffInBlock );
#endif
        m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( compID, pcRecoYuv, uiAbsPartIdx, tuRect.width, tuRect.height );
      }
    } // End of channel loop

  }
  else
  {
    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xSetIntraResultQT( bLumaOnly, pcRecoYuv, tuRecurseChild );
    } while (tuRecurseChild.nextSection(rTu));
  }
}


Void
TEncSearch::xStoreIntraResultQT(const ComponentID first,
                                const ComponentID lastIncl,
                                      TComTU &rTu )
{
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiTrDepth = rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if (  first==COMPONENT_Y || uiTrMode == uiTrDepth )
  {
    assert(uiTrMode == uiTrDepth);
    const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    const UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;


    for(UInt compID_=first; compID_<=lastIncl; compID_++)
    {
      ComponentID compID=ComponentID(compID_);
      if (rTu.ProcessComponentSection(compID))
      {
        const TComRectangle &tuRect=rTu.getRect(compID);

        //===== copy transform coefficients =====
        const UInt uiNumCoeff    = tuRect.width * tuRect.height;
        TCoeff* pcCoeffSrc = m_ppcQTTempCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
        TCoeff* pcCoeffDst = m_pcQTTempTUCoeff[compID];

        ::memcpy( pcCoeffDst, pcCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#if ADAPTIVE_QP_SELECTION
        TCoeff* pcArlCoeffSrc = m_ppcQTTempArlCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
        TCoeff* pcArlCoeffDst = m_ppcQTTempTUArlCoeff[compID];
        ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#endif
        //===== copy reconstruction =====
        m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( compID, &m_pcQTTempTransformSkipTComYuv, uiAbsPartIdx, tuRect.width, tuRect.height );
      }
    }
  }
}


Void
TEncSearch::xLoadIntraResultQT(const ComponentID first,
                               const ComponentID lastIncl,
                                     TComTU &rTu)
{
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiTrDepth = rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if (  first==COMPONENT_Y || uiTrMode == uiTrDepth )
  {
    assert(uiTrMode == uiTrDepth);
    const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    const UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
    const UInt uiZOrder     = pcCU->getZorderIdxInCU() + uiAbsPartIdx;

    for(UInt compID_=first; compID_<=lastIncl; compID_++)
    {
      ComponentID compID=ComponentID(compID_);
      if (rTu.ProcessComponentSection(compID))
      {
        const TComRectangle &tuRect=rTu.getRect(compID);

        //===== copy transform coefficients =====
        const UInt uiNumCoeff = tuRect.width * tuRect.height;
        TCoeff* pcCoeffDst = m_ppcQTTempCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
        TCoeff* pcCoeffSrc = m_pcQTTempTUCoeff[compID];

        ::memcpy( pcCoeffDst, pcCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#if ADAPTIVE_QP_SELECTION
        TCoeff* pcArlCoeffDst = m_ppcQTTempArlCoeff[compID] [ uiQTLayer ] + rTu.getCoefficientOffset(compID);
        TCoeff* pcArlCoeffSrc = m_ppcQTTempTUArlCoeff[compID];
        ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * uiNumCoeff );
#endif
        //===== copy reconstruction =====
        m_pcQTTempTransformSkipTComYuv.copyPartToPartComponent( compID, &m_pcQTTempTComYuv[ uiQTLayer ], uiAbsPartIdx, tuRect.width, tuRect.height );

        Pel*    piRecIPred        = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getAddr(), uiZOrder );
        UInt    uiRecIPredStride  = pcCU->getPic()->getPicYuvRec()->getStride (compID);
        Pel*    piRecQt           = m_pcQTTempTComYuv[ uiQTLayer ].getAddr( compID, uiAbsPartIdx );
        UInt    uiRecQtStride     = m_pcQTTempTComYuv[ uiQTLayer ].getStride  (compID);
        UInt    uiWidth           = tuRect.width;
        UInt    uiHeight          = tuRect.height;
        Pel* pRecQt               = piRecQt;
        Pel* pRecIPred            = piRecIPred;
        for( UInt uiY = 0; uiY < uiHeight; uiY++ )
        {
          for( UInt uiX = 0; uiX < uiWidth; uiX++ )
          {
            pRecIPred[ uiX ] = pRecQt   [ uiX ];
          }
          pRecQt    += uiRecQtStride;
          pRecIPred += uiRecIPredStride;
        }
      }
    }
  }
}



Void
TEncSearch::xRecurIntraChromaCodingQT(TComYuv*    pcOrgYuv,
                                      TComYuv*    pcPredYuv,
                                      TComYuv*    pcResiYuv,
                                      Distortion& ruiDist,
                                      TComTU&     rTu
                                      DEBUG_STRING_FN_DECLARE(sDebug))
{
  TComDataCU         *pcCU                  = rTu.getCU();
  const UInt          uiTrDepth             = rTu.GetTransformDepthRel();
  const UInt          uiAbsPartIdx          = rTu.GetAbsPartIdxTU();
  const ChromaFormat  format                = rTu.GetChromaFormat();
  UInt                uiTrMode              = pcCU->getTransformIdx( uiAbsPartIdx );
  const UInt          numberValidComponents = getNumberValidComponents(format);

  if(  uiTrMode == uiTrDepth )
  {
    if (!rTu.ProcessChannelSection(CHANNEL_TYPE_CHROMA)) return;

    const UInt uiFullDepth = rTu.GetTransformDepthTotal();

    Bool checkTransformSkip = pcCU->getSlice()->getPPS()->getUseTransformSkip();

#if RExt__N0288_SPECIFY_TRANSFORM_SKIP_MAXIMUM_SIZE
    checkTransformSkip &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Cb), pcCU->getSlice()->getPPS()->getTransformSkipLog2MaxSize());
#else
    checkTransformSkip &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Cb));
#endif

    if ( m_pcEncCfg->getUseTransformSkipFast() )
    {
#if RExt__N0288_SPECIFY_TRANSFORM_SKIP_MAXIMUM_SIZE
      checkTransformSkip &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Y), pcCU->getSlice()->getPPS()->getTransformSkipLog2MaxSize());
#else
      checkTransformSkip &= TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(COMPONENT_Y)); // is the current case a 4x4 Luma case, if not no TS. If so, then only TS if at least one corresponding Luma TS is set.
#endif
      if (checkTransformSkip)
      {
        Int nbLumaSkip = 0;
        const UInt maxAbsPartIdxSub=uiAbsPartIdx + (rTu.ProcessingAllQuadrants(COMPONENT_Cb)?1:4);
        for(UInt absPartIdxSub = uiAbsPartIdx; absPartIdxSub < maxAbsPartIdxSub; absPartIdxSub ++)
        {
          nbLumaSkip += pcCU->getTransformSkip(absPartIdxSub, COMPONENT_Y);
        }
        checkTransformSkip &= (nbLumaSkip > 0);
      }
    }


    for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      const ComponentID compID = ComponentID(ch);
      DEBUG_STRING_NEW(sDebugBestMode)

      //use RDO to decide whether Cr/Cb takes TS
      if( m_bUseSBACRD && checkTransformSkip )
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[uiFullDepth][CI_QT_TRAFO_ROOT] );
      }

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 == 0)
      const UInt totalAdjustedDepthChan   = rTu.GetTransformDepthTotalAdj(compID);
#else
      const Bool splitIntoSubTUs = rTu.getRect(compID).width != rTu.getRect(compID).height;

      TComTURecurse TUIterator(rTu, false, (splitIntoSubTUs ? TComTU::VERTICAL_SPLIT : TComTU::DONT_SPLIT), true, compID);

      const UInt partIdxesPerSubTU = TUIterator.GetAbsPartIdxNumParts(compID);

      do
      {
        const UInt subTUAbsPartIdx   = TUIterator.GetAbsPartIdxTU(compID);
#endif
        if (checkTransformSkip)
        {
          Double     dSingleCost    = MAX_DOUBLE;
          Int        bestModeId     = 0;
          Distortion singleDistC    = 0;
          UInt       singleCbfC     = 0;
          Distortion singleDistCTmp = 0;
          Double     singleCostTmp  = 0;
          UInt       singleCbfCTmp  = 0;

          Int        default0Save1Load2 = 0;
          Int        firstCheckId       = 0;

          for(Int chromaModeId = firstCheckId; chromaModeId < 2; chromaModeId ++)
          {
            DEBUG_STRING_NEW(sDebugMode)
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            pcCU->setTransformSkipPartRange( chromaModeId, compID, subTUAbsPartIdx, partIdxesPerSubTU );
#else
            pcCU->setTransformSkipSubParts ( chromaModeId, compID, uiAbsPartIdx, totalAdjustedDepthChan );
#endif
            if(chromaModeId == firstCheckId)
            {
              default0Save1Load2 = 1;
            }
            else
            {
              default0Save1Load2 = 2;
            }
            singleDistCTmp = 0;

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, singleDistCTmp, compID, TUIterator DEBUG_STRING_PASS_INTO(sDebugMode), default0Save1Load2);
            singleCbfCTmp = pcCU->getCbf( subTUAbsPartIdx, compID, uiTrDepth);
#else
            xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, singleDistCTmp, compID, rTu DEBUG_STRING_PASS_INTO(sDebugMode), default0Save1Load2);
            singleCbfCTmp = pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepth);
#endif

            if(chromaModeId == 1 && singleCbfCTmp == 0)
            {
              //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
              singleCostTmp = MAX_DOUBLE;
            }
            else
            {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
              UInt bitsTmp = xGetIntraBitsQTChroma( TUIterator, compID, false );
#else
              UInt bitsTmp = xGetIntraBitsQTChroma( rTu, compID, false );
#endif
              singleCostTmp  = m_pcRdCost->calcRdCost( bitsTmp, singleDistCTmp);
            }

            if(singleCostTmp < dSingleCost)
            {
              DEBUG_STRING_SWAP(sDebugBestMode, sDebugMode)
              dSingleCost = singleCostTmp;
              singleDistC = singleDistCTmp;
              bestModeId  = chromaModeId;
              singleCbfC  = singleCbfCTmp;

              if(bestModeId == firstCheckId)
              {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
                xStoreIntraResultQT(compID, compID, TUIterator);
#else
                xStoreIntraResultQT(compID, compID, rTu);
#endif
                if( m_bUseSBACRD)
                {
                  m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
                }
              }
            }
            if(chromaModeId == firstCheckId)
            {
              m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiFullDepth ][ CI_QT_TRAFO_ROOT ] );
            }
          }

          if(bestModeId == firstCheckId)
          {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            xLoadIntraResultQT(compID, compID, TUIterator);
            pcCU->setCbfPartRange( singleCbfC << uiTrDepth, compID, subTUAbsPartIdx, partIdxesPerSubTU );
#else
            xLoadIntraResultQT(compID, compID, rTu);
            pcCU->setCbfSubParts ( singleCbfC << uiTrDepth, compID, uiAbsPartIdx, totalAdjustedDepthChan );
#endif

            if(m_bUseSBACRD)
            {
              m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiFullDepth ][ CI_TEMP_BEST ] );
            }
          }

          DEBUG_STRING_APPEND(sDebug, sDebugBestMode)
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          pcCU ->setTransformSkipPartRange( bestModeId, compID, subTUAbsPartIdx, partIdxesPerSubTU );
#else
          pcCU ->setTransformSkipSubParts( bestModeId, compID, uiAbsPartIdx, totalAdjustedDepthChan );
#endif
          ruiDist += singleDistC;
        }
        else //not checking transform skip
        {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          pcCU ->setTransformSkipPartRange( 0, compID, subTUAbsPartIdx, partIdxesPerSubTU );
          xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, ruiDist, compID, TUIterator DEBUG_STRING_PASS_INTO(sDebug) );
#else
          pcCU ->setTransformSkipSubParts( 0, compID, uiAbsPartIdx, totalAdjustedDepthChan );
          xIntraCodingTUBlock( pcOrgYuv, pcPredYuv, pcResiYuv, ruiDist, compID, rTu DEBUG_STRING_PASS_INTO(sDebug) );
#endif
        }
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
      }
      while (TUIterator.nextSection(rTu));

      if (splitIntoSubTUs) offsetSubTUCBFs(rTu, compID);
#endif
    }
  }
  else
  {
    UInt    uiSplitCbf[MAX_NUM_COMPONENT] = {0,0,0};

    TComTURecurse tuRecurseChild(rTu, false);
    const UInt uiTrDepthChild   = tuRecurseChild.GetTransformDepthRel();
    do
    {
      DEBUG_STRING_NEW(sChild)
      xRecurIntraChromaCodingQT( pcOrgYuv, pcPredYuv, pcResiYuv, ruiDist, tuRecurseChild DEBUG_STRING_PASS_INTO(sChild) );
      DEBUG_STRING_APPEND(sDebug, sChild)
      const UInt uiAbsPartIdxSub=tuRecurseChild.GetAbsPartIdxTU();

      for(UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
      {
        uiSplitCbf[ch] |= pcCU->getCbf( uiAbsPartIdxSub, ComponentID(ch), uiTrDepthChild );
      }
    } while ( tuRecurseChild.nextSection(rTu) );


    UInt uiPartsDiv = rTu.GetAbsPartIdxNumParts();
    for(UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      if (uiSplitCbf[ch])
      {
        const UInt flag=1<<uiTrDepth;
        ComponentID compID=ComponentID(ch);
        UChar *pBase=pcCU->getCbf( compID );
        for( UInt uiOffs = 0; uiOffs < uiPartsDiv; uiOffs++ )
        {
          pBase[ uiAbsPartIdx + uiOffs ] |= flag;
        }
      }
    }
  }
}




Void
TEncSearch::xSetIntraResultChromaQT(TComYuv*    pcRecoYuv, TComTU &rTu)
{
  if (!rTu.ProcessChannelSection(CHANNEL_TYPE_CHROMA)) return;
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiTrDepth   = rTu.GetTransformDepthRel();
  UInt uiTrMode     = pcCU->getTransformIdx( uiAbsPartIdx );
  if(  uiTrMode == uiTrDepth )
  {
    UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    UInt uiQTLayer    = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    //===== copy transform coefficients =====
    const TComRectangle &tuRectCb=rTu.getRect(COMPONENT_Cb);
    UInt uiNumCoeffC    = tuRectCb.width*tuRectCb.height;//( pcCU->getSlice()->getSPS()->getMaxCUWidth() * pcCU->getSlice()->getSPS()->getMaxCUHeight() ) >> ( uiFullDepth << 1 );
    const UInt offset = rTu.getCoefficientOffset(COMPONENT_Cb);

    const UInt numberValidComponents = getNumberValidComponents(rTu.GetChromaFormat());
    for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
    {
      const ComponentID component = ComponentID(ch);
      const TCoeff* src           = m_ppcQTTempCoeff[component][uiQTLayer] + offset;//(uiNumCoeffIncC*uiAbsPartIdx);
      TCoeff* dest                = pcCU->getCoeff(component) + offset;//(uiNumCoeffIncC*uiAbsPartIdx);
      ::memcpy( dest, src, sizeof(TCoeff)*uiNumCoeffC );
#if ADAPTIVE_QP_SELECTION
      TCoeff* pcArlCoeffSrc = m_ppcQTTempArlCoeff[component][ uiQTLayer ] + offset;//( uiNumCoeffIncC * uiAbsPartIdx );
      TCoeff* pcArlCoeffDst = pcCU->getArlCoeff(component)                + offset;//( uiNumCoeffIncC * uiAbsPartIdx );
      ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * uiNumCoeffC );
#endif
    }

    //===== copy reconstruction =====

    m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( COMPONENT_Cb, pcRecoYuv, uiAbsPartIdx, tuRectCb.width, tuRectCb.height );
    m_pcQTTempTComYuv[ uiQTLayer ].copyPartToPartComponent( COMPONENT_Cr, pcRecoYuv, uiAbsPartIdx, tuRectCb.width, tuRectCb.height );
  }
  else
  {
    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xSetIntraResultChromaQT( pcRecoYuv, tuRecurseChild );
    } while (tuRecurseChild.nextSection(rTu));
  }
}



Void
TEncSearch::preestChromaPredMode( TComDataCU* pcCU,
                                 TComYuv*    pcOrgYuv,
                                 TComYuv*    pcPredYuv )
{

  //===== loop over partitions =====
  const UInt    uiInitTrDepth  = pcCU->getPartitionSize(0) != SIZE_2Nx2N && enable4ChromaPUsInIntraNxNCU(pcOrgYuv->getChromaFormat()) ? 1 : 0;
  TComTURecurse tuRecurseCU(pcCU, 0);
  TComTURecurse tuRecurseWithPU(tuRecurseCU, false, (uiInitTrDepth==0)?TComTU::DONT_SPLIT : TComTU::QUAD_SPLIT);
  const ChromaFormat chFmt = tuRecurseWithPU.GetChromaFormat();
#if RExt__N0080_INTRA_REFERENCE_SMOOTHING_DISABLED_FLAG
  Bool bFilterEnabled=filterIntraReferenceSamples(CHANNEL_TYPE_CHROMA, chFmt, pcCU->getSlice()->getSPS()->getDisableIntraReferenceSmoothing());
#else
  Bool bFilterEnabled=filterIntraReferenceSamples(CHANNEL_TYPE_CHROMA, chFmt);
#endif

  do
  {
    if (tuRecurseWithPU.ProcessChannelSection(CHANNEL_TYPE_CHROMA))
    {
      const TComRectangle &rect=tuRecurseWithPU.getRect(COMPONENT_Cb);
      const UInt  uiWidth     = rect.width;
      const UInt  uiHeight    = rect.height;
      const UInt  partIdx     = tuRecurseWithPU.GetAbsPartIdxCU();
      const UInt  uiStride    = pcOrgYuv ->getStride(COMPONENT_Cb);
      Pel*  piOrgU      = pcOrgYuv ->getAddr ( COMPONENT_Cb, partIdx ); //TODO: RExt - Change this into an array and loop over chroma components below
      Pel*  piOrgV      = pcOrgYuv ->getAddr ( COMPONENT_Cr, partIdx );
      Pel*  piPredU     = pcPredYuv->getAddr ( COMPONENT_Cb, partIdx );
      Pel*  piPredV     = pcPredYuv->getAddr ( COMPONENT_Cr, partIdx );

      //===== init pattern =====
      Bool  bAboveAvail = false;
      Bool  bLeftAvail  = false;
      DEBUG_STRING_NEW(sTemp)
      initAdiPatternChType( tuRecurseWithPU, bAboveAvail, bLeftAvail, COMPONENT_Cb, bFilterEnabled DEBUG_STRING_PASS_INTO(sTemp) );
      initAdiPatternChType( tuRecurseWithPU, bAboveAvail, bLeftAvail, COMPONENT_Cr, bFilterEnabled DEBUG_STRING_PASS_INTO(sTemp) );

      //===== get best prediction modes (using SAD) =====
            UInt        uiMinMode          = 0;
            UInt        uiMaxMode          = 4;
            UInt        uiBestMode         = MAX_UINT;
            Distortion  uiMinSAD           = std::numeric_limits<Distortion>::max();
      const UInt        mappedModeTable[4] = {PLANAR_IDX,DC_IDX,HOR_IDX,VER_IDX};

      for( UInt uiMode_  = uiMinMode; uiMode_ < uiMaxMode; uiMode_++ )
      {
        UInt uiMode=mappedModeTable[uiMode_];
        //--- get prediction ---
#if RExt__N0080_INTRA_REFERENCE_SMOOTHING_DISABLED_FLAG
        const Bool bUseFilter=TComPrediction::filteringIntraReferenceSamples(COMPONENT_Cb, uiMode, uiWidth, uiHeight, chFmt, pcCU->getSlice()->getSPS()->getDisableIntraReferenceSmoothing());
#else
        const Bool bUseFilter=TComPrediction::filteringIntraReferenceSamples(COMPONENT_Cb, uiMode, uiWidth, uiHeight, chFmt);
#endif

        predIntraAng( COMPONENT_Cb, uiMode, piOrgU, uiStride, piPredU, uiStride, tuRecurseCU, bAboveAvail, bLeftAvail, bUseFilter );
        predIntraAng( COMPONENT_Cr, uiMode, piOrgV, uiStride, piPredV, uiStride, tuRecurseCU, bAboveAvail, bLeftAvail, bUseFilter );

        //--- get SAD ---
        Distortion uiSAD  = m_pcRdCost->calcHAD( g_bitDepth[CHANNEL_TYPE_CHROMA], piOrgU, uiStride, piPredU, uiStride, uiWidth, uiHeight );
        uiSAD            += m_pcRdCost->calcHAD( g_bitDepth[CHANNEL_TYPE_CHROMA], piOrgV, uiStride, piPredV, uiStride, uiWidth, uiHeight );
        //--- check ---
        if( uiSAD < uiMinSAD )
        {
          uiMinSAD   = uiSAD;
          uiBestMode = uiMode;
        }
      }

      //===== set chroma pred mode =====
      pcCU->setIntraDirSubParts( CHANNEL_TYPE_CHROMA, uiBestMode, partIdx, tuRecurseWithPU.getCUDepth() + uiInitTrDepth );
    }
  } while (tuRecurseWithPU.nextSection(tuRecurseCU));
}




Void
TEncSearch::estIntraPredQT(TComDataCU* pcCU,
                           TComYuv*    pcOrgYuv,
                           TComYuv*    pcPredYuv,
                           TComYuv*    pcResiYuv,
                           TComYuv*    pcRecoYuv,
                           Distortion& ruiDistC,
                           Bool        bLumaOnly
                           DEBUG_STRING_FN_DECLARE(sDebug))
{
  const UInt         uiDepth               = pcCU->getDepth(0);
  const UInt         uiInitTrDepth         = pcCU->getPartitionSize(0) == SIZE_2Nx2N ? 0 : 1;
  const UInt         uiInitTrDepthC        = pcCU->getPartitionSize(0) != SIZE_2Nx2N && enable4ChromaPUsInIntraNxNCU(pcOrgYuv->getChromaFormat()) ? 1 : 0;
  const UInt         uiNumPU               = 1<<(2*uiInitTrDepth);
  const UInt         uiQNumParts           = pcCU->getTotalNumPart() >> 2;
  const UInt         uiWidthBit            = pcCU->getIntraSizeIdx(0);
  const ChromaFormat chFmt                 = pcCU->getPic()->getChromaFormat();
  const UInt         numberValidComponents = getNumberValidComponents(chFmt);
        Distortion   uiOverallDistY        = 0;
        Distortion   uiOverallDistC        = 0;
        UInt         CandNum;
        Double       CandCostList[ FAST_UDI_MAX_RDMODE_NUM ];
#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
  //NOTE: RExt - Lambda calculation at equivalent Qp of 4 is recommended because at that Qp, the quantisation divisor is 1.
#if FULL_NBIT
  const Double sqrtLambdaForFirstPass= (m_pcEncCfg->getCostMode()==COST_MIXED_LOSSLESS_LOSSY_CODING && pcCU->getCUTransquantBypass(0)) ?
                sqrt(0.57 * pow(2.0, ((RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_TEST_QP_PRIME - 12) / 3.0)))
              : m_pcRdCost->getSqrtLambda();
#else
  const Double sqrtLambdaForFirstPass= (m_pcEncCfg->getCostMode()==COST_MIXED_LOSSLESS_LOSSY_CODING && pcCU->getCUTransquantBypass(0)) ?
                sqrt(0.57 * pow(2.0, ((RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_TEST_QP_PRIME - 12 - 6 * (g_bitDepth[CHANNEL_TYPE_LUMA] - 8)) / 3.0)))
              : m_pcRdCost->getSqrtLambda();
#endif
#endif

  //===== set QP and clear Cbf =====
  if ( pcCU->getSlice()->getPPS()->getUseDQP() == true)
  {
    pcCU->setQPSubParts( pcCU->getQP(0), 0, uiDepth );
  }
  else
  {
    pcCU->setQPSubParts( pcCU->getSlice()->getSliceQp(), 0, uiDepth );
  }

  //===== loop over partitions =====
  TComTURecurse tuRecurseCU(pcCU, 0);
  TComTURecurse tuRecurseWithPU(tuRecurseCU, false, (uiInitTrDepth==0)?TComTU::DONT_SPLIT : TComTU::QUAD_SPLIT);

  do
  {
    const UInt uiPartOffset=tuRecurseWithPU.GetAbsPartIdxTU();
//  for( UInt uiPU = 0, uiPartOffset=0; uiPU < uiNumPU; uiPU++, uiPartOffset += uiQNumParts )
  //{
    //===== init pattern for luma prediction =====
    Bool bAboveAvail = false;
    Bool bLeftAvail  = false;
    DEBUG_STRING_NEW(sTemp2)

    //===== determine set of modes to be tested (using prediction signal only) =====
    Int numModesAvailable     = 35; //total number of Intra modes
    UInt uiRdModeList[FAST_UDI_MAX_RDMODE_NUM];
    Int numModesForFullRD = g_aucIntraModeNumFast[ uiWidthBit ];

    if (tuRecurseWithPU.ProcessComponentSection(COMPONENT_Y))
      initAdiPatternChType( tuRecurseWithPU, bAboveAvail, bLeftAvail, COMPONENT_Y, true DEBUG_STRING_PASS_INTO(sTemp2) );

    Bool doFastSearch = (numModesForFullRD != numModesAvailable);
    if (doFastSearch)
    {
      assert(numModesForFullRD < numModesAvailable);

      for( Int i=0; i < numModesForFullRD; i++ )
      {
        CandCostList[ i ] = MAX_DOUBLE;
      }
      CandNum = 0;

      const TComRectangle &puRect=tuRecurseWithPU.getRect(COMPONENT_Y);
      const UInt uiAbsPartIdx=tuRecurseWithPU.GetAbsPartIdxTU();

      Pel* piOrg         = pcOrgYuv ->getAddr( COMPONENT_Y, uiAbsPartIdx );
      Pel* piPred        = pcPredYuv->getAddr( COMPONENT_Y, uiAbsPartIdx );
      UInt uiStride      = pcPredYuv->getStride( COMPONENT_Y );

      for( Int modeIdx = 0; modeIdx < numModesAvailable; modeIdx++ )
      {
        UInt       uiMode = modeIdx;
        Distortion uiSad  = 0;

#if RExt__N0080_INTRA_REFERENCE_SMOOTHING_DISABLED_FLAG
        const Bool bUseFilter=TComPrediction::filteringIntraReferenceSamples(COMPONENT_Y, uiMode, puRect.width, puRect.height, chFmt, pcCU->getSlice()->getSPS()->getDisableIntraReferenceSmoothing());
#else
        const Bool bUseFilter=TComPrediction::filteringIntraReferenceSamples(COMPONENT_Y, uiMode, puRect.width, puRect.height, chFmt);
#endif
        predIntraAng( COMPONENT_Y, uiMode, piOrg, uiStride, piPred, uiStride, tuRecurseWithPU, bAboveAvail, bLeftAvail, bUseFilter );

        // use hadamard transform here
        uiSad+=m_pcRdCost->calcHAD( g_bitDepth[toChannelType(COMPONENT_Y)], piOrg, uiStride, piPred, uiStride, puRect.width, puRect.height );

        UInt   iModeBits = 0;

        // NB xModeBitsIntra will not affect the mode for chroma that may have already been pre-estimated.
        iModeBits+=xModeBitsIntra( pcCU, uiMode, uiPartOffset, uiDepth, uiInitTrDepth, CHANNEL_TYPE_LUMA );

#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
        Double cost      = (Double)uiSad + (Double)iModeBits * sqrtLambdaForFirstPass;
#else
        Double cost      = (Double)uiSad + (Double)iModeBits * m_pcRdCost->getSqrtLambda();
#endif

#ifdef DEBUG_INTRA_SEARCH_COSTS
        std::cout << "1st pass mode " << uiMode << " SAD = " << uiSad << ", mode bits = " << iModeBits << ", cost = " << cost << "\n";
        exit(0);
#endif

        CandNum += xUpdateCandList( uiMode, cost, numModesForFullRD, uiRdModeList, CandCostList );
      }

#if FAST_UDI_USE_MPM
      Int uiPreds[NUM_MOST_PROBABLE_MODES] = {-1, -1, -1};

      Int iMode = -1;
      Int numCand = pcCU->getIntraDirPredictor( uiPartOffset, uiPreds, COMPONENT_Y, &iMode );

      if( iMode >= 0 )
      {
        numCand = iMode;
      }

      for( Int j=0; j < numCand; j++)

      {
        Bool mostProbableModeIncluded = false;
        Int mostProbableMode = uiPreds[j];

        for( Int i=0; i < numModesForFullRD; i++)
        {
          mostProbableModeIncluded |= (mostProbableMode == uiRdModeList[i]);
        }
        if (!mostProbableModeIncluded)
        {
          uiRdModeList[numModesForFullRD++] = mostProbableMode;
        }
      }
#endif // FAST_UDI_USE_MPM
    }
    else
    {
      for( Int i=0; i < numModesForFullRD; i++)
      {
        uiRdModeList[i] = i;
      }
    }

    //===== check modes (using r-d costs) =====
#if HHI_RQT_INTRA_SPEEDUP_MOD
    UInt   uiSecondBestMode  = MAX_UINT;
    Double dSecondBestPUCost = MAX_DOUBLE;
#endif
    DEBUG_STRING_NEW(sPU)
    UInt       uiBestPUMode  = 0;
    Distortion uiBestPUDistY = 0;
    Distortion uiBestPUDistC = 0;
    Double     dBestPUCost   = MAX_DOUBLE;

#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
    UInt max=numModesForFullRD;

    if (DebugOptionList::ForceLumaMode.isSet()) max=0;  // we are forcing a direction, so don't bother with mode check
    for ( UInt uiMode = 0; uiMode < max; uiMode++)
#else
    for( UInt uiMode = 0; uiMode < numModesForFullRD; uiMode++ )
#endif
    {
      // set luma prediction mode
      UInt uiOrgMode = uiRdModeList[uiMode];

      pcCU->setIntraDirSubParts ( CHANNEL_TYPE_LUMA, uiOrgMode, uiPartOffset, uiDepth + uiInitTrDepth );

      DEBUG_STRING_NEW(sMode)
      // set context models
      if( m_bUseSBACRD )
      {
        m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST] );
      }

      // determine residual for partition
      Distortion uiPUDistY = 0;
      Distortion uiPUDistC = 0;
      Double     dPUCost   = 0.0;
#if HHI_RQT_INTRA_SPEEDUP
      xRecurIntraCodingQT( bLumaOnly, pcOrgYuv, pcPredYuv, pcResiYuv, uiPUDistY, uiPUDistC, true, dPUCost, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sMode) );
#else
      xRecurIntraCodingQT( bLumaOnly, pcOrgYuv, pcPredYuv, pcResiYuv, uiPUDistY, uiPUDistC, dPUCost, tuRecurseWithPU );
#endif

#ifdef DEBUG_INTRA_SEARCH_COSTS
      std::cout << "2nd pass [luma,chroma] mode [" << Int(pcCU->getIntraDir(CHANNEL_TYPE_LUMA, uiPartOffset)) << "," << Int(pcCU->getIntraDir(CHANNEL_TYPE_CHROMA, uiPartOffset)) << "] cost = " << dPUCost << "\n";
#endif

      // check r-d cost
      if( dPUCost < dBestPUCost )
      {
        DEBUG_STRING_SWAP(sPU, sMode)
#if HHI_RQT_INTRA_SPEEDUP_MOD
        uiSecondBestMode  = uiBestPUMode;
        dSecondBestPUCost = dBestPUCost;
#endif
        uiBestPUMode  = uiOrgMode;
        uiBestPUDistY = uiPUDistY;
        uiBestPUDistC = uiPUDistC;
        dBestPUCost   = dPUCost;

        xSetIntraResultQT( bLumaOnly, pcRecoYuv, tuRecurseWithPU );

        UInt uiQPartNum = tuRecurseWithPU.GetAbsPartIdxNumParts();

        ::memcpy( m_puhQTTempTrIdx,  pcCU->getTransformIdx()       + uiPartOffset, uiQPartNum * sizeof( UChar ) );
        for (UInt component = 0; component < numberValidComponents; component++)
        {
          const ComponentID compID = ComponentID(component);
          ::memcpy( m_puhQTTempCbf[compID], pcCU->getCbf( compID  ) + uiPartOffset, uiQPartNum * sizeof( UChar ) );
          ::memcpy( m_puhQTTempTransformSkipFlag[compID],  pcCU->getTransformSkip(compID)  + uiPartOffset, uiQPartNum * sizeof( UChar ) );
        }
      }
#if HHI_RQT_INTRA_SPEEDUP_MOD
      else if( dPUCost < dSecondBestPUCost )
      {
        uiSecondBestMode  = uiOrgMode;
        dSecondBestPUCost = dPUCost;
      }
#endif
    } // Mode loop

#if HHI_RQT_INTRA_SPEEDUP
#if HHI_RQT_INTRA_SPEEDUP_MOD
    for( UInt ui =0; ui < 2; ++ui )
#endif
    {
#if HHI_RQT_INTRA_SPEEDUP_MOD
      UInt uiOrgMode   = ui ? uiSecondBestMode  : uiBestPUMode;
      if( uiOrgMode == MAX_UINT )
      {
        break;
      }
#else
      UInt uiOrgMode = uiBestPUMode;
#endif

#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
      if (DebugOptionList::ForceLumaMode.isSet())
        uiOrgMode = DebugOptionList::ForceLumaMode.getInt();
#endif

      pcCU->setIntraDirSubParts ( CHANNEL_TYPE_LUMA, uiOrgMode, uiPartOffset, uiDepth + uiInitTrDepth );
      DEBUG_STRING_NEW(sModeTree)

      // set context models
      if( m_bUseSBACRD )
      {
        m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST] );
      }

      // determine residual for partition
      Distortion uiPUDistY = 0;
      Distortion uiPUDistC = 0;
      Double     dPUCost   = 0.0;

      xRecurIntraCodingQT( bLumaOnly, pcOrgYuv, pcPredYuv, pcResiYuv, uiPUDistY, uiPUDistC, false, dPUCost, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sModeTree));

      // check r-d cost
      if( dPUCost < dBestPUCost )
      {
        DEBUG_STRING_SWAP(sPU, sModeTree)
        uiBestPUMode  = uiOrgMode;
        uiBestPUDistY = uiPUDistY;
        uiBestPUDistC = uiPUDistC;
        dBestPUCost   = dPUCost;

        xSetIntraResultQT( bLumaOnly, pcRecoYuv, tuRecurseWithPU );

        const UInt uiQPartNum = tuRecurseWithPU.GetAbsPartIdxNumParts();
        ::memcpy( m_puhQTTempTrIdx,  pcCU->getTransformIdx()       + uiPartOffset, uiQPartNum * sizeof( UChar ) );

        for (UInt component = 0; component < numberValidComponents; component++)
        {
          const ComponentID compID = ComponentID(component);
          ::memcpy( m_puhQTTempCbf[compID], pcCU->getCbf( compID  ) + uiPartOffset, uiQPartNum * sizeof( UChar ) );
          ::memcpy( m_puhQTTempTransformSkipFlag[compID],  pcCU->getTransformSkip(compID)  + uiPartOffset, uiQPartNum * sizeof( UChar ) );
        }
      }
    } // Mode loop
#endif

    DEBUG_STRING_APPEND(sDebug, sPU)

    //--- update overall distortion ---
    uiOverallDistY += uiBestPUDistY;
    uiOverallDistC += uiBestPUDistC;

    //--- update transform index and cbf ---
    const UInt uiQPartNum = tuRecurseWithPU.GetAbsPartIdxNumParts();
    ::memcpy( pcCU->getTransformIdx()       + uiPartOffset, m_puhQTTempTrIdx,  uiQPartNum * sizeof( UChar ) );
    for (UInt component = 0; component < numberValidComponents; component++)
    {
      const ComponentID compID = ComponentID(component);
      ::memcpy( pcCU->getCbf( compID  ) + uiPartOffset, m_puhQTTempCbf[compID], uiQPartNum * sizeof( UChar ) );
      ::memcpy( pcCU->getTransformSkip( compID  ) + uiPartOffset, m_puhQTTempTransformSkipFlag[compID ], uiQPartNum * sizeof( UChar ) );
    }

    //--- set reconstruction for next intra prediction blocks ---
    if( !tuRecurseWithPU.IsLastSection() )
    {
      const Bool bSkipChroma  = tuRecurseWithPU.ProcessChannelSection(CHANNEL_TYPE_CHROMA);

      const UInt numChannelToProcess = (bLumaOnly || bSkipChroma) ? 1 : getNumberValidComponents(pcCU->getPic()->getChromaFormat());

      for (UInt ch=0; ch<numChannelToProcess; ch++)
      {
        const ComponentID compID = ComponentID(ch);
        const TComRectangle &puRect=tuRecurseWithPU.getRect(compID);
        const UInt  uiCompWidth   = puRect.width;
        const UInt  uiCompHeight  = puRect.height;

        const UInt  uiZOrder      = pcCU->getZorderIdxInCU() + uiPartOffset;
              Pel*  piDes         = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getAddr(), uiZOrder );
        const UInt  uiDesStride   = pcCU->getPic()->getPicYuvRec()->getStride( compID);
        const Pel*  piSrc         = pcRecoYuv->getAddr( compID, uiPartOffset );
        const UInt  uiSrcStride   = pcRecoYuv->getStride( compID);

        for( UInt uiY = 0; uiY < uiCompHeight; uiY++, piSrc += uiSrcStride, piDes += uiDesStride )
        {
          for( UInt uiX = 0; uiX < uiCompWidth; uiX++ )
          {
            piDes[ uiX ] = piSrc[ uiX ];
          }
        }
      }
    }

    //=== update PU data ====
    pcCU->setIntraDirSubParts     ( CHANNEL_TYPE_LUMA, uiBestPUMode, uiPartOffset, uiDepth + uiInitTrDepth );
    if (!bLumaOnly && getChromasCorrespondingPULumaIdx(uiPartOffset, chFmt)==uiPartOffset)
    {
      UInt chromaDir=pcCU->getIntraDir(CHANNEL_TYPE_CHROMA, getChromasCorrespondingPULumaIdx(uiPartOffset, chFmt));
      if (chromaDir == uiBestPUMode && tuRecurseWithPU.ProcessChannelSection(CHANNEL_TYPE_CHROMA))
      {
        pcCU->setIntraDirSubParts     ( CHANNEL_TYPE_CHROMA, DM_CHROMA_IDX, getChromasCorrespondingPULumaIdx(uiPartOffset, chFmt), uiDepth + uiInitTrDepthC );
      }
    }
    //pcCU->copyToPic                   ( uiDepth, uiPU, uiInitTrDepth ); // Unnecessary copy?
  } while (tuRecurseWithPU.nextSection(tuRecurseCU));


  if( uiNumPU > 1 )
  { // set Cbf for all blocks
    UInt uiCombCbfY = 0;
    UInt uiCombCbfU = 0;
    UInt uiCombCbfV = 0;
    UInt uiPartIdx  = 0;
    for( UInt uiPart = 0; uiPart < 4; uiPart++, uiPartIdx += uiQNumParts )
    {
      uiCombCbfY |= pcCU->getCbf( uiPartIdx, COMPONENT_Y,  1 );
      uiCombCbfU |= pcCU->getCbf( uiPartIdx, COMPONENT_Cb, 1 );
      uiCombCbfV |= pcCU->getCbf( uiPartIdx, COMPONENT_Cr, 1 );
    }
    for( UInt uiOffs = 0; uiOffs < 4 * uiQNumParts; uiOffs++ )
    {
      pcCU->getCbf( COMPONENT_Y  )[ uiOffs ] |= uiCombCbfY;
      pcCU->getCbf( COMPONENT_Cb )[ uiOffs ] |= uiCombCbfU;
      pcCU->getCbf( COMPONENT_Cr )[ uiOffs ] |= uiCombCbfV;
    }
  }

  //===== reset context models =====
  if(m_bUseSBACRD)
  {
    m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);
  }

  //===== set distortion (rate and r-d costs are determined later) =====
  ruiDistC                   = uiOverallDistC;
  pcCU->getTotalDistortion() = uiOverallDistY + uiOverallDistC;
}




Void
TEncSearch::estIntraPredChromaQT(TComDataCU* pcCU,
                                 TComYuv*    pcOrgYuv,
                                 TComYuv*    pcPredYuv,
                                 TComYuv*    pcResiYuv,
                                 TComYuv*    pcRecoYuv,
                                 Distortion  uiPreCalcDistC
                                 DEBUG_STRING_FN_DECLARE(sDebug))
{
  pcCU->getTotalDistortion      () -= uiPreCalcDistC;

  //const UInt    uiDepthCU     = pcCU->getDepth(0);
  const UInt    uiInitTrDepth  = pcCU->getPartitionSize(0) != SIZE_2Nx2N && enable4ChromaPUsInIntraNxNCU(pcOrgYuv->getChromaFormat()) ? 1 : 0;
//  const UInt    uiNumPU        = 1<<(2*uiInitTrDepth);

  TComTURecurse tuRecurseCU(pcCU, 0);
  TComTURecurse tuRecurseWithPU(tuRecurseCU, false, (uiInitTrDepth==0)?TComTU::DONT_SPLIT : TComTU::QUAD_SPLIT);
  const UInt    uiQNumParts    = tuRecurseWithPU.GetAbsPartIdxNumParts();
  const UInt    uiDepthCU=tuRecurseWithPU.getCUDepth();
  const UInt    numberValidComponents = pcCU->getPic()->getNumberValidComponents();

  do
  {
    UInt       uiBestMode  = 0;
    Distortion uiBestDist  = 0;
    Double     dBestCost   = MAX_DOUBLE;

    //----- init mode list -----
    if (tuRecurseWithPU.ProcessChannelSection(CHANNEL_TYPE_CHROMA))
    {
      UInt uiModeList[FAST_UDI_MAX_RDMODE_NUM];
      const UInt  uiQPartNum     = uiQNumParts;
      const UInt  uiPartOffset   = tuRecurseWithPU.GetAbsPartIdxTU();
      {
        UInt  uiMinMode = 0;
        UInt  uiMaxMode = NUM_CHROMA_MODE;

        //----- check chroma modes -----
        pcCU->getAllowedChromaDir( uiPartOffset, uiModeList );

#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
        if (DebugOptionList::ForceChromaMode.isSet())
        {
          uiMinMode=DebugOptionList::ForceChromaMode.getInt();
          if (uiModeList[uiMinMode]==34) uiMinMode=5; // if the fixed mode has been renumbered because DM_CHROMA covers it, use DM_CHROMA.
          uiMaxMode=uiMinMode+1;
        }
#endif

        DEBUG_STRING_NEW(sPU)

        for( UInt uiMode = uiMinMode; uiMode < uiMaxMode; uiMode++ )
        {
          //----- restore context models -----
          if( m_bUseSBACRD )
          {
            m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepthCU][CI_CURR_BEST] );
          }
          DEBUG_STRING_NEW(sMode)
          //----- chroma coding -----
          Distortion uiDist = 0;
          pcCU->setIntraDirSubParts  ( CHANNEL_TYPE_CHROMA, uiModeList[uiMode], uiPartOffset, uiDepthCU+uiInitTrDepth );
          xRecurIntraChromaCodingQT       ( pcOrgYuv, pcPredYuv, pcResiYuv, uiDist, tuRecurseWithPU DEBUG_STRING_PASS_INTO(sMode) );

          if( m_bUseSBACRD && pcCU->getSlice()->getPPS()->getUseTransformSkip() )
          {
            m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepthCU][CI_CURR_BEST] );
          }

          UInt    uiBits = xGetIntraBitsQT( tuRecurseWithPU, false, true, false );
          Double  dCost  = m_pcRdCost->calcRdCost( uiBits, uiDist );

          //----- compare -----
          if( dCost < dBestCost )
          {
            DEBUG_STRING_SWAP(sPU, sMode);
            dBestCost   = dCost;
            uiBestDist  = uiDist;
            uiBestMode  = uiModeList[uiMode];

            xSetIntraResultChromaQT( pcRecoYuv, tuRecurseWithPU );
            for (UInt componentIndex = COMPONENT_Cb; componentIndex < numberValidComponents; componentIndex++)
            {
              const ComponentID compID = ComponentID(componentIndex);
              ::memcpy( m_puhQTTempCbf[compID], pcCU->getCbf( compID )+uiPartOffset, uiQPartNum * sizeof( UChar ) );
              ::memcpy( m_puhQTTempTransformSkipFlag[compID], pcCU->getTransformSkip( compID )+uiPartOffset, uiQPartNum * sizeof( UChar ) );
            }
          }
        }

        DEBUG_STRING_APPEND(sDebug, sPU)

        //----- set data -----
        for (UInt componentIndex = COMPONENT_Cb; componentIndex < numberValidComponents; componentIndex++)
        {
          const ComponentID compID = ComponentID(componentIndex);
          ::memcpy( pcCU->getCbf( compID )+uiPartOffset, m_puhQTTempCbf[compID], uiQPartNum * sizeof( UChar ) );
          ::memcpy( pcCU->getTransformSkip( compID )+uiPartOffset, m_puhQTTempTransformSkipFlag[compID], uiQPartNum * sizeof( UChar ) );
        }
      }

      if( ! tuRecurseWithPU.IsLastSection() )
      {
        for (UInt ch=COMPONENT_Cb; ch<numberValidComponents; ch++)
        {
          const ComponentID compID    = ComponentID(ch);
          const TComRectangle &tuRect = tuRecurseWithPU.getRect(compID);
          const UInt  uiCompWidth     = tuRect.width;
          const UInt  uiCompHeight    = tuRect.height;
          const UInt  uiZOrder        = pcCU->getZorderIdxInCU() + tuRecurseWithPU.GetAbsPartIdxTU();
                Pel*  piDes           = pcCU->getPic()->getPicYuvRec()->getAddr( compID, pcCU->getAddr(), uiZOrder );
          const UInt  uiDesStride     = pcCU->getPic()->getPicYuvRec()->getStride( compID);
          const Pel*  piSrc           = pcRecoYuv->getAddr( compID, uiPartOffset );
          const UInt  uiSrcStride     = pcRecoYuv->getStride( compID);

          for( UInt uiY = 0; uiY < uiCompHeight; uiY++, piSrc += uiSrcStride, piDes += uiDesStride )
          {
            for( UInt uiX = 0; uiX < uiCompWidth; uiX++ )
            {
              piDes[ uiX ] = piSrc[ uiX ];
            }
          }
        }
      }

      pcCU->setIntraDirSubParts( CHANNEL_TYPE_CHROMA, uiBestMode, uiPartOffset, uiDepthCU+uiInitTrDepth );
      pcCU->getTotalDistortion      () += uiBestDist;
    }

  } while (tuRecurseWithPU.nextSection(tuRecurseCU));

  //----- restore context models -----

  if( uiInitTrDepth != 0 )
  { // set Cbf for all blocks
    UInt uiCombCbfU = 0;
    UInt uiCombCbfV = 0;
    UInt uiPartIdx  = 0;
    for( UInt uiPart = 0; uiPart < 4; uiPart++, uiPartIdx += uiQNumParts )
    {
      uiCombCbfU |= pcCU->getCbf( uiPartIdx, COMPONENT_Cb, 1 );
      uiCombCbfV |= pcCU->getCbf( uiPartIdx, COMPONENT_Cr, 1 );
    }
    for( UInt uiOffs = 0; uiOffs < 4 * uiQNumParts; uiOffs++ )
    {
      pcCU->getCbf( COMPONENT_Cb )[ uiOffs ] |= uiCombCbfU;
      pcCU->getCbf( COMPONENT_Cr )[ uiOffs ] |= uiCombCbfV;
    }
  }

  if( m_bUseSBACRD )
  {
    m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[uiDepthCU][CI_CURR_BEST] );
  }
}




/** Function for encoding and reconstructing luma/chroma samples of a PCM mode CU.
 * \param pcCU pointer to current CU
 * \param uiAbsPartIdx part index
 * \param piOrg pointer to original sample arrays
 * \param piPCM pointer to PCM code arrays
 * \param piPred pointer to prediction signal arrays
 * \param piResi pointer to residual signal arrays
 * \param piReco pointer to reconstructed sample arrays
 * \param uiStride stride of the original/prediction/residual sample arrays
 * \param uiWidth block width
 * \param uiHeight block height
 * \param ttText texture component type
 * \returns Void
 */
Void TEncSearch::xEncPCM (TComDataCU* pcCU, UInt uiAbsPartIdx, Pel* pOrg, Pel* pPCM, Pel* pPred, Pel* pResi, Pel* pReco, UInt uiStride, UInt uiWidth, UInt uiHeight, const ComponentID compID )
{
  const UInt uiReconStride = pcCU->getPic()->getPicYuvRec()->getStride(compID);
  const UInt uiPCMBitDepth = pcCU->getSlice()->getSPS()->getPCMBitDepth(toChannelType(compID));
  Pel* pRecoPic = pcCU->getPic()->getPicYuvRec()->getAddr(compID, pcCU->getAddr(), pcCU->getZorderIdxInCU()+uiAbsPartIdx);

  const Int pcmShiftRight=(g_bitDepth[toChannelType(compID)] - Int(uiPCMBitDepth));

  assert(pcmShiftRight >= 0);

  for( UInt uiY = 0; uiY < uiHeight; uiY++ )
  {
    for( UInt uiX = 0; uiX < uiWidth; uiX++ )
    {
      // Reset pred and residual
      pPred[uiX] = 0;
      pResi[uiX] = 0;
      // Encode
      pPCM[uiX] = (pOrg[uiX]>>pcmShiftRight);
      // Reconstruction
      pReco   [uiX] = (pPCM[uiX]<<(pcmShiftRight));
      pRecoPic[uiX] = pReco[uiX];
    }
    pPred += uiStride;
    pResi += uiStride;
    pPCM += uiWidth;
    pOrg += uiStride;
    pReco += uiStride;
    pRecoPic += uiReconStride;
  }
}


/**  Function for PCM mode estimation.
 * \param pcCU
 * \param pcOrgYuv
 * \param rpcPredYuv
 * \param rpcResiYuv
 * \param rpcRecoYuv
 * \returns Void
 */
Void TEncSearch::IPCMSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv*& rpcPredYuv, TComYuv*& rpcResiYuv, TComYuv*& rpcRecoYuv )
{
  UInt        uiDepth      = pcCU->getDepth(0);
  const UInt  uiDistortion = 0;
  UInt        uiBits;

  Double dCost;

  for (UInt ch=0; ch < pcCU->getPic()->getNumberValidComponents(); ch++)
  {
    const ComponentID compID  = ComponentID(ch);
    const UInt width  = pcCU->getWidth(0)  >> pcCU->getPic()->getComponentScaleX(compID);
    const UInt height = pcCU->getHeight(0) >> pcCU->getPic()->getComponentScaleY(compID);
    const UInt stride = rpcPredYuv->getStride(compID);

    Pel * pOrig    = pcOrgYuv->getAddr  (compID, 0, width);
    Pel * pResi    = rpcResiYuv->getAddr(compID, 0, width);
    Pel * pPred    = rpcPredYuv->getAddr(compID, 0, width);
    Pel * pReco    = rpcRecoYuv->getAddr(compID, 0, width);
    Pel * pPCM     = pcCU->getPCMSample (compID);

    xEncPCM ( pcCU, 0, pOrig, pPCM, pPred, pResi, pReco, stride, width, height, compID );

  }

  m_pcEntropyCoder->resetBits();
  xEncIntraHeader ( pcCU, uiDepth, 0, true, false);
  uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();

  dCost = m_pcRdCost->calcRdCost( uiBits, uiDistortion );

  if(m_bUseSBACRD)
  {
    m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);
  }

  pcCU->getTotalBits()       = uiBits;
  pcCU->getTotalCost()       = dCost;
  pcCU->getTotalDistortion() = uiDistortion;

  pcCU->copyToPic(uiDepth, 0, 0);
}




Void TEncSearch::xGetInterPredictionError( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPartIdx, Distortion& ruiErr, Bool bHadamard )
{
  motionCompensation( pcCU, &m_tmpYuvPred, REF_PIC_LIST_X, iPartIdx );

  UInt uiAbsPartIdx = 0;
  Int iWidth = 0;
  Int iHeight = 0;
  pcCU->getPartIndexAndSize( iPartIdx, uiAbsPartIdx, iWidth, iHeight );

  DistParam cDistParam;

  cDistParam.bApplyWeight = false;

  m_pcRdCost->setDistParam( cDistParam, g_bitDepth[CHANNEL_TYPE_LUMA],
                            pcYuvOrg->getAddr( COMPONENT_Y, uiAbsPartIdx ), pcYuvOrg->getStride(COMPONENT_Y),
                            m_tmpYuvPred .getAddr( COMPONENT_Y, uiAbsPartIdx ), m_tmpYuvPred .getStride(COMPONENT_Y),
#if NS_HAD
                            iWidth, iHeight, m_pcEncCfg->getUseHADME(), m_pcEncCfg->getUseNSQT() );
#else
                            iWidth, iHeight, m_pcEncCfg->getUseHADME() );
#endif
  ruiErr = cDistParam.DistFunc( &cDistParam );
}

/** estimation of best merge coding
 * \param pcCU
 * \param pcYuvOrg
 * \param iPUIdx
 * \param uiInterDir
 * \param pacMvField
 * \param uiMergeIndex
 * \param ruiCost
 * \param ruiBits
 * \param puhNeighCands
 * \param bValid
 * \returns Void
 */
Void TEncSearch::xMergeEstimation( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPUIdx, UInt& uiInterDir, TComMvField* pacMvField, UInt& uiMergeIndex, Distortion& ruiCost, TComMvField* cMvFieldNeighbours, UChar* uhInterDirNeighbours, Int& numValidMergeCand )
{
  UInt uiAbsPartIdx = 0;
  Int iWidth = 0;
  Int iHeight = 0;

  pcCU->getPartIndexAndSize( iPUIdx, uiAbsPartIdx, iWidth, iHeight );
  UInt uiDepth = pcCU->getDepth( uiAbsPartIdx );

  PartSize partSize = pcCU->getPartitionSize( 0 );
  if ( pcCU->getSlice()->getPPS()->getLog2ParallelMergeLevelMinus2() && partSize != SIZE_2Nx2N && pcCU->getWidth( 0 ) <= 8 )
  {
    pcCU->setPartSizeSubParts( SIZE_2Nx2N, 0, uiDepth );
    if ( iPUIdx == 0 )
    {
      pcCU->getInterMergeCandidates( 0, 0, cMvFieldNeighbours,uhInterDirNeighbours, numValidMergeCand );
    }
    pcCU->setPartSizeSubParts( partSize, 0, uiDepth );
  }
  else
  {
    pcCU->getInterMergeCandidates( uiAbsPartIdx, iPUIdx, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand );
  }

  xRestrictBipredMergeCand( pcCU, iPUIdx, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand );

  ruiCost = std::numeric_limits<Distortion>::max();
  for( UInt uiMergeCand = 0; uiMergeCand < numValidMergeCand; ++uiMergeCand )
  {
    {
      Distortion uiCostCand = std::numeric_limits<Distortion>::max();
      UInt       uiBitsCand = 0;

      PartSize ePartSize = pcCU->getPartitionSize( 0 );

      pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField( cMvFieldNeighbours[0 + 2*uiMergeCand], ePartSize, uiAbsPartIdx, 0, iPUIdx );
      pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField( cMvFieldNeighbours[1 + 2*uiMergeCand], ePartSize, uiAbsPartIdx, 0, iPUIdx );

      xGetInterPredictionError( pcCU, pcYuvOrg, iPUIdx, uiCostCand, m_pcEncCfg->getUseHADME() );
      uiBitsCand = uiMergeCand + 1;
      if (uiMergeCand == m_pcEncCfg->getMaxNumMergeCand() -1)
      {
         uiBitsCand--;
      }
      uiCostCand = uiCostCand + m_pcRdCost->getCost( uiBitsCand );
      if ( uiCostCand < ruiCost )
      {
        ruiCost = uiCostCand;
        pacMvField[0] = cMvFieldNeighbours[0 + 2*uiMergeCand];
        pacMvField[1] = cMvFieldNeighbours[1 + 2*uiMergeCand];
        uiInterDir = uhInterDirNeighbours[uiMergeCand];
        uiMergeIndex = uiMergeCand;
      }
    }
  }
}

/** convert bi-pred merge candidates to uni-pred
 * \param pcCU
 * \param puIdx
 * \param mvFieldNeighbours
 * \param interDirNeighbours
 * \param numValidMergeCand
 * \returns Void
 */
Void TEncSearch::xRestrictBipredMergeCand( TComDataCU* pcCU, UInt puIdx, TComMvField* mvFieldNeighbours, UChar* interDirNeighbours, Int numValidMergeCand )
{
  if ( pcCU->isBipredRestriction(puIdx) )
  {
    for( UInt mergeCand = 0; mergeCand < numValidMergeCand; ++mergeCand )
    {
      if ( interDirNeighbours[mergeCand] == 3 )
      {
        interDirNeighbours[mergeCand] = 1;
        mvFieldNeighbours[(mergeCand << 1) + 1].setMvField(TComMv(0,0), -1);
      }
    }
  }
}

/** search of the best candidate for inter prediction
 * \param pcCU
 * \param pcOrgYuv
 * \param rpcPredYuv
 * \param rpcResiYuv
 * \param rpcRecoYuv
 * \param bUseRes
 * \returns Void
 */
#if AMP_MRG
Void TEncSearch::predInterSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv*& rpcPredYuv, TComYuv*& rpcResiYuv, TComYuv*& rpcRecoYuv DEBUG_STRING_FN_DECLARE(sDebug), Bool bUseRes, Bool bUseMRG )
#else
Void TEncSearch::predInterSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv*& rpcPredYuv, TComYuv*& rpcResiYuv, TComYuv*& rpcRecoYuv, Bool bUseRes )
#endif
{
  for(UInt i=0; i<NUM_REF_PIC_LIST_01; i++)
  {
    m_acYuvPred[i].clear();
  }
  m_cYuvPredTemp.clear();
  rpcPredYuv->clear();

  if ( !bUseRes )
  {
    rpcResiYuv->clear();
  }

  rpcRecoYuv->clear();

  TComMv       cMvSrchRngLT;
  TComMv       cMvSrchRngRB;

  TComMv       cMvZero;
  TComMv       TempMv; //kolya

  TComMv       cMv[2];
  TComMv       cMvBi[2];
  TComMv       cMvTemp[2][33];

  Int          iNumPart    = pcCU->getNumPartInter();
  Int          iNumPredDir = pcCU->getSlice()->isInterP() ? 1 : 2;

  TComMv       cMvPred[2][33];

  TComMv       cMvPredBi[2][33];
  Int          aaiMvpIdxBi[2][33];

  Int          aaiMvpIdx[2][33];
  Int          aaiMvpNum[2][33];

  AMVPInfo     aacAMVPInfo[2][33];

  Int          iRefIdx[2]={0,0}; //If un-initialized, may cause SEGV in bi-directional prediction iterative stage.
  Int          iRefIdxBi[2];

  UInt         uiPartAddr;
  Int          iRoiWidth, iRoiHeight;

  UInt         uiMbBits[3] = {1, 1, 0};

  UInt         uiLastMode = 0;
  Int          iRefStart, iRefEnd;

  PartSize     ePartSize = pcCU->getPartitionSize( 0 );

  Int          bestBiPRefIdxL1 = 0;
  Int          bestBiPMvpL1 = 0;
  Distortion   biPDistTemp = std::numeric_limits<Distortion>::max();

#if ZERO_MVD_EST
  Int          aiZeroMvdMvpIdx[2] = {-1, -1};
  Int          aiZeroMvdRefIdx[2] = {0, 0};
  Int          iZeroMvdDir = -1;
#endif

  TComMvField cMvFieldNeighbours[MRG_MAX_NUM_CANDS << 1]; // double length for mv of both lists
  UChar uhInterDirNeighbours[MRG_MAX_NUM_CANDS];
  Int numValidMergeCand = 0 ;

  for ( Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++ )
  {
    Distortion   uiCost[2] = { std::numeric_limits<Distortion>::max(), std::numeric_limits<Distortion>::max() };
    Distortion   uiCostBi  =   std::numeric_limits<Distortion>::max();
    Distortion   uiCostTemp;

    UInt         uiBits[3];
    UInt         uiBitsTemp;
#if ZERO_MVD_EST
    Distortion   uiZeroMvdCost = std::numeric_limits<Distortion>::max();
    Distortion   uiZeroMvdCostTemp;
    UInt         uiZeroMvdBitsTemp;
    Distortion   uiZeroMvdDistTemp = std::numeric_limits<Distortion>::max();
    UInt         auiZeroMvdBits[3];
#endif
    Distortion   bestBiPDist = std::numeric_limits<Distortion>::max();

    Distortion   uiCostTempL0[MAX_NUM_REF];
    for (Int iNumRef=0; iNumRef < MAX_NUM_REF; iNumRef++) uiCostTempL0[iNumRef] = std::numeric_limits<Distortion>::max();
    UInt         uiBitsTempL0[MAX_NUM_REF];

    TComMv       mvValidList1;
    Int          refIdxValidList1 = 0;
    UInt         bitsValidList1 = MAX_UINT;
    Distortion   costValidList1 = std::numeric_limits<Distortion>::max();

    xGetBlkBits( ePartSize, pcCU->getSlice()->isInterP(), iPartIdx, uiLastMode, uiMbBits);

    pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iRoiWidth, iRoiHeight );

#if AMP_MRG
    Bool bTestNormalMC = true;

    if ( bUseMRG && pcCU->getWidth( 0 ) > 8 && iNumPart == 2 )
    {
      bTestNormalMC = false;
    }

    if (bTestNormalMC)
    {
#endif

    //  Uni-directional prediction
    for ( Int iRefList = 0; iRefList < iNumPredDir; iRefList++ )
    {
      RefPicList  eRefPicList = ( iRefList ? REF_PIC_LIST_1 : REF_PIC_LIST_0 );

      for ( Int iRefIdxTemp = 0; iRefIdxTemp < pcCU->getSlice()->getNumRefIdx(eRefPicList); iRefIdxTemp++ )
      {
        uiBitsTemp = uiMbBits[iRefList];
        if ( pcCU->getSlice()->getNumRefIdx(eRefPicList) > 1 )
        {
          uiBitsTemp += iRefIdxTemp+1;
          if ( iRefIdxTemp == pcCU->getSlice()->getNumRefIdx(eRefPicList)-1 ) uiBitsTemp--;
        }
#if ZERO_MVD_EST
        xEstimateMvPredAMVP( pcCU, pcOrgYuv, iPartIdx, eRefPicList, iRefIdxTemp, cMvPred[iRefList][iRefIdxTemp], false, &biPDistTemp, &uiZeroMvdDistTemp);
#else
        xEstimateMvPredAMVP( pcCU, pcOrgYuv, iPartIdx, eRefPicList, iRefIdxTemp, cMvPred[iRefList][iRefIdxTemp], false, &biPDistTemp);
#endif
        aaiMvpIdx[iRefList][iRefIdxTemp] = pcCU->getMVPIdx(eRefPicList, uiPartAddr);
        aaiMvpNum[iRefList][iRefIdxTemp] = pcCU->getMVPNum(eRefPicList, uiPartAddr);

        if(pcCU->getSlice()->getMvdL1ZeroFlag() && iRefList==1 && biPDistTemp < bestBiPDist)
        {
          bestBiPDist = biPDistTemp;
          bestBiPMvpL1 = aaiMvpIdx[iRefList][iRefIdxTemp];
          bestBiPRefIdxL1 = iRefIdxTemp;
        }

        uiBitsTemp += m_auiMVPIdxCost[aaiMvpIdx[iRefList][iRefIdxTemp]][AMVP_MAX_NUM_CANDS];
#if ZERO_MVD_EST
        if ( iRefList == 0 || pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp ) < 0 )
        {
          uiZeroMvdBitsTemp = uiBitsTemp;
          uiZeroMvdBitsTemp += 2; //zero mvd bits

#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
          m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
          m_pcRdCost->getMotionCost( 1, 0 );
#endif
          uiZeroMvdCostTemp = uiZeroMvdDistTemp + m_pcRdCost->getCost(uiZeroMvdBitsTemp);

          if (uiZeroMvdCostTemp < uiZeroMvdCost)
          {
            uiZeroMvdCost = uiZeroMvdCostTemp;
            iZeroMvdDir = iRefList + 1;
            aiZeroMvdRefIdx[iRefList] = iRefIdxTemp;
            aiZeroMvdMvpIdx[iRefList] = aaiMvpIdx[iRefList][iRefIdxTemp];
            auiZeroMvdBits[iRefList] = uiZeroMvdBitsTemp;
          }
        }
#endif

#if GPB_SIMPLE_UNI
        if ( iRefList == 1 )    // list 1
        {
          if ( pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp ) >= 0 )
          {
            cMvTemp[1][iRefIdxTemp] = cMvTemp[0][pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp )];
            uiCostTemp = uiCostTempL0[pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp )];
            /*first subtract the bit-rate part of the cost of the other list*/
            uiCostTemp -= m_pcRdCost->getCost( uiBitsTempL0[pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp )] );
            /*correct the bit-rate part of the current ref*/
            m_pcRdCost->setPredictor  ( cMvPred[iRefList][iRefIdxTemp] );
            uiBitsTemp += m_pcRdCost->getBits( cMvTemp[1][iRefIdxTemp].getHor(), cMvTemp[1][iRefIdxTemp].getVer() );
            /*calculate the correct cost*/
            uiCostTemp += m_pcRdCost->getCost( uiBitsTemp );
          }
          else
          {
            xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPred[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp );
          }
        }
        else
        {
          xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPred[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp );
        }
#else
        xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPred[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp );
#endif
        xCopyAMVPInfo(pcCU->getCUMvField(eRefPicList)->getAMVPInfo(), &aacAMVPInfo[iRefList][iRefIdxTemp]); // must always be done ( also when AMVP_MODE = AM_NONE )
        xCheckBestMVP(pcCU, eRefPicList, cMvTemp[iRefList][iRefIdxTemp], cMvPred[iRefList][iRefIdxTemp], aaiMvpIdx[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp);

        if ( iRefList == 0 )
        {
          uiCostTempL0[iRefIdxTemp] = uiCostTemp;
          uiBitsTempL0[iRefIdxTemp] = uiBitsTemp;
        }
        if ( uiCostTemp < uiCost[iRefList] )
        {
          uiCost[iRefList] = uiCostTemp;
          uiBits[iRefList] = uiBitsTemp; // storing for bi-prediction

          // set motion
          cMv[iRefList]     = cMvTemp[iRefList][iRefIdxTemp];
          iRefIdx[iRefList] = iRefIdxTemp;
        }

        if ( iRefList == 1 && uiCostTemp < costValidList1 && pcCU->getSlice()->getList1IdxToList0Idx( iRefIdxTemp ) < 0 )
        {
          costValidList1 = uiCostTemp;
          bitsValidList1 = uiBitsTemp;

          // set motion
          mvValidList1     = cMvTemp[iRefList][iRefIdxTemp];
          refIdxValidList1 = iRefIdxTemp;
        }
      }
    }

    //  Bi-directional prediction
    if ( (pcCU->getSlice()->isInterB()) && (pcCU->isBipredRestriction(iPartIdx) == false) )
    {

      cMvBi[0] = cMv[0];            cMvBi[1] = cMv[1];
      iRefIdxBi[0] = iRefIdx[0];    iRefIdxBi[1] = iRefIdx[1];

      ::memcpy(cMvPredBi, cMvPred, sizeof(cMvPred));
      ::memcpy(aaiMvpIdxBi, aaiMvpIdx, sizeof(aaiMvpIdx));

      UInt uiMotBits[2];

      if(pcCU->getSlice()->getMvdL1ZeroFlag())
      {
        xCopyAMVPInfo(&aacAMVPInfo[1][bestBiPRefIdxL1], pcCU->getCUMvField(REF_PIC_LIST_1)->getAMVPInfo());
        pcCU->setMVPIdxSubParts( bestBiPMvpL1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        aaiMvpIdxBi[1][bestBiPRefIdxL1] = bestBiPMvpL1;
        cMvPredBi[1][bestBiPRefIdxL1]   = pcCU->getCUMvField(REF_PIC_LIST_1)->getAMVPInfo()->m_acMvCand[bestBiPMvpL1];

        cMvBi[1] = cMvPredBi[1][bestBiPRefIdxL1];
        iRefIdxBi[1] = bestBiPRefIdxL1;
        pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMv( cMvBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
        pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllRefIdx( iRefIdxBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
        TComYuv* pcYuvPred = &m_acYuvPred[REF_PIC_LIST_1];
        motionCompensation( pcCU, pcYuvPred, REF_PIC_LIST_1, iPartIdx );

        uiMotBits[0] = uiBits[0] - uiMbBits[0];
        uiMotBits[1] = uiMbBits[1];

        if ( pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1) > 1 )
        {
          uiMotBits[1] += bestBiPRefIdxL1+1;
          if ( bestBiPRefIdxL1 == pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1)-1 ) uiMotBits[1]--;
        }

        uiMotBits[1] += m_auiMVPIdxCost[aaiMvpIdxBi[1][bestBiPRefIdxL1]][AMVP_MAX_NUM_CANDS];

        uiBits[2] = uiMbBits[2] + uiMotBits[0] + uiMotBits[1];

        cMvTemp[1][bestBiPRefIdxL1] = cMvBi[1];
      }
      else
      {
        uiMotBits[0] = uiBits[0] - uiMbBits[0];
        uiMotBits[1] = uiBits[1] - uiMbBits[1];
        uiBits[2] = uiMbBits[2] + uiMotBits[0] + uiMotBits[1];
      }

      // 4-times iteration (default)
      Int iNumIter = 4;

      // fast encoder setting: only one iteration
      if ( m_pcEncCfg->getUseFastEnc() || pcCU->getSlice()->getMvdL1ZeroFlag())
      {
        iNumIter = 1;
      }

      for ( Int iIter = 0; iIter < iNumIter; iIter++ )
      {
        Int         iRefList    = iIter % 2;

        if ( m_pcEncCfg->getUseFastEnc() )
        {
          if( uiCost[0] <= uiCost[1] )
          {
            iRefList = 1;
          }
          else
          {
            iRefList = 0;
          }
        }
        else if ( iIter == 0 )
        {
          iRefList = 0;
        }
        if ( iIter == 0 && !pcCU->getSlice()->getMvdL1ZeroFlag())
        {
          pcCU->getCUMvField(RefPicList(1-iRefList))->setAllMv( cMv[1-iRefList], ePartSize, uiPartAddr, 0, iPartIdx );
          pcCU->getCUMvField(RefPicList(1-iRefList))->setAllRefIdx( iRefIdx[1-iRefList], ePartSize, uiPartAddr, 0, iPartIdx );
          TComYuv*  pcYuvPred = &m_acYuvPred[1-iRefList];
          motionCompensation ( pcCU, pcYuvPred, RefPicList(1-iRefList), iPartIdx );
        }

        RefPicList  eRefPicList = ( iRefList ? REF_PIC_LIST_1 : REF_PIC_LIST_0 );

        if(pcCU->getSlice()->getMvdL1ZeroFlag())
        {
          iRefList = 0;
          eRefPicList = REF_PIC_LIST_0;
        }

        Bool bChanged = false;

        iRefStart = 0;
        iRefEnd   = pcCU->getSlice()->getNumRefIdx(eRefPicList)-1;

        for ( Int iRefIdxTemp = iRefStart; iRefIdxTemp <= iRefEnd; iRefIdxTemp++ )
        {
          uiBitsTemp = uiMbBits[2] + uiMotBits[1-iRefList];
          if ( pcCU->getSlice()->getNumRefIdx(eRefPicList) > 1 )
          {
            uiBitsTemp += iRefIdxTemp+1;
            if ( iRefIdxTemp == pcCU->getSlice()->getNumRefIdx(eRefPicList)-1 ) uiBitsTemp--;
          }
          uiBitsTemp += m_auiMVPIdxCost[aaiMvpIdxBi[iRefList][iRefIdxTemp]][AMVP_MAX_NUM_CANDS];
          // call ME
          xMotionEstimation ( pcCU, pcOrgYuv, iPartIdx, eRefPicList, &cMvPredBi[iRefList][iRefIdxTemp], iRefIdxTemp, cMvTemp[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp, true );

          xCopyAMVPInfo(&aacAMVPInfo[iRefList][iRefIdxTemp], pcCU->getCUMvField(eRefPicList)->getAMVPInfo());
          xCheckBestMVP(pcCU, eRefPicList, cMvTemp[iRefList][iRefIdxTemp], cMvPredBi[iRefList][iRefIdxTemp], aaiMvpIdxBi[iRefList][iRefIdxTemp], uiBitsTemp, uiCostTemp);

          if ( uiCostTemp < uiCostBi )
          {
            bChanged = true;

            cMvBi[iRefList]     = cMvTemp[iRefList][iRefIdxTemp];
            iRefIdxBi[iRefList] = iRefIdxTemp;

            uiCostBi            = uiCostTemp;
            uiMotBits[iRefList] = uiBitsTemp - uiMbBits[2] - uiMotBits[1-iRefList];
            uiBits[2]           = uiBitsTemp;

            if(iNumIter!=1)
            {
              //  Set motion
              pcCU->getCUMvField( eRefPicList )->setAllMv( cMvBi[iRefList], ePartSize, uiPartAddr, 0, iPartIdx );
              pcCU->getCUMvField( eRefPicList )->setAllRefIdx( iRefIdxBi[iRefList], ePartSize, uiPartAddr, 0, iPartIdx );

              TComYuv* pcYuvPred = &m_acYuvPred[iRefList];
              motionCompensation( pcCU, pcYuvPred, eRefPicList, iPartIdx );
            }
          }
        } // for loop-iRefIdxTemp

        if ( !bChanged )
        {
          if ( uiCostBi <= uiCost[0] && uiCostBi <= uiCost[1] )
          {
            xCopyAMVPInfo(&aacAMVPInfo[0][iRefIdxBi[0]], pcCU->getCUMvField(REF_PIC_LIST_0)->getAMVPInfo());
            xCheckBestMVP(pcCU, REF_PIC_LIST_0, cMvBi[0], cMvPredBi[0][iRefIdxBi[0]], aaiMvpIdxBi[0][iRefIdxBi[0]], uiBits[2], uiCostBi);
            if(!pcCU->getSlice()->getMvdL1ZeroFlag())
            {
              xCopyAMVPInfo(&aacAMVPInfo[1][iRefIdxBi[1]], pcCU->getCUMvField(REF_PIC_LIST_1)->getAMVPInfo());
              xCheckBestMVP(pcCU, REF_PIC_LIST_1, cMvBi[1], cMvPredBi[1][iRefIdxBi[1]], aaiMvpIdxBi[1][iRefIdxBi[1]], uiBits[2], uiCostBi);
            }
          }
          break;
        }
      } // for loop-iter
    } // if (B_SLICE)
#if ZERO_MVD_EST
    if ( (pcCU->getSlice()->isInterB()) && (pcCU->isBipredRestriction(iPartIdx) == false) )
    {
#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
      m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
      m_pcRdCost->getMotionCost( 1, 0 );
#endif

      for ( Int iL0RefIdxTemp = 0; iL0RefIdxTemp <= pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_0)-1; iL0RefIdxTemp++ )
      for ( Int iL1RefIdxTemp = 0; iL1RefIdxTemp <= pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1)-1; iL1RefIdxTemp++ )
      {
        UInt uiRefIdxBitsTemp = 0;
        if ( pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_0) > 1 )
        {
          uiRefIdxBitsTemp += iL0RefIdxTemp+1;
          if ( iL0RefIdxTemp == pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_0)-1 ) uiRefIdxBitsTemp--;
        }
        if ( pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1) > 1 )
        {
          uiRefIdxBitsTemp += iL1RefIdxTemp+1;
          if ( iL1RefIdxTemp == pcCU->getSlice()->getNumRefIdx(REF_PIC_LIST_1)-1 ) uiRefIdxBitsTemp--;
        }

        Int iL0MVPIdx = 0;
        Int iL1MVPIdx = 0;

        for (iL0MVPIdx = 0; iL0MVPIdx < aaiMvpNum[0][iL0RefIdxTemp]; iL0MVPIdx++)
        {
          for (iL1MVPIdx = 0; iL1MVPIdx < aaiMvpNum[1][iL1RefIdxTemp]; iL1MVPIdx++)
          {
            uiZeroMvdBitsTemp = uiRefIdxBitsTemp;
            uiZeroMvdBitsTemp += uiMbBits[2];
            uiZeroMvdBitsTemp += m_auiMVPIdxCost[iL0MVPIdx][aaiMvpNum[0][iL0RefIdxTemp]] + m_auiMVPIdxCost[iL1MVPIdx][aaiMvpNum[1][iL1RefIdxTemp]];
            uiZeroMvdBitsTemp += 4; //zero mvd for both directions
            pcCU->getCUMvField( REF_PIC_LIST_0 )->setAllMvField( aacAMVPInfo[0][iL0RefIdxTemp].m_acMvCand[iL0MVPIdx], iL0RefIdxTemp, ePartSize, uiPartAddr, iPartIdx, 0 );
            pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMvField( aacAMVPInfo[1][iL1RefIdxTemp].m_acMvCand[iL1MVPIdx], iL1RefIdxTemp, ePartSize, uiPartAddr, iPartIdx, 0 );

            xGetInterPredictionError( pcCU, pcOrgYuv, iPartIdx, uiZeroMvdDistTemp, m_pcEncCfg->getUseHADME() );
            uiZeroMvdCostTemp = uiZeroMvdDistTemp + m_pcRdCost->getCost( uiZeroMvdBitsTemp );
            if (uiZeroMvdCostTemp < uiZeroMvdCost)
            {
              uiZeroMvdCost = uiZeroMvdCostTemp;
              iZeroMvdDir = 3;
              aiZeroMvdMvpIdx[0] = iL0MVPIdx;
              aiZeroMvdMvpIdx[1] = iL1MVPIdx;
              aiZeroMvdRefIdx[0] = iL0RefIdxTemp;
              aiZeroMvdRefIdx[1] = iL1RefIdxTemp;
              auiZeroMvdBits[2] = uiZeroMvdBitsTemp;
            }
          }
        }
      }
    }
#endif

#if AMP_MRG
    } //end if bTestNormalMC
#endif
    //  Clear Motion Field
    pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField( TComMvField(), ePartSize, uiPartAddr, 0, iPartIdx );
    pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField( TComMvField(), ePartSize, uiPartAddr, 0, iPartIdx );
    pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( cMvZero,       ePartSize, uiPartAddr, 0, iPartIdx );
    pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( cMvZero,       ePartSize, uiPartAddr, 0, iPartIdx );

    pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

    UInt uiMEBits = 0;
    // Set Motion Field_
    cMv[1] = mvValidList1;
    iRefIdx[1] = refIdxValidList1;
    uiBits[1] = bitsValidList1;
    uiCost[1] = costValidList1;

#if AMP_MRG
    if (bTestNormalMC)
    {
#endif
#if ZERO_MVD_EST
    if (uiZeroMvdCost <= uiCostBi && uiZeroMvdCost <= uiCost[0] && uiZeroMvdCost <= uiCost[1])
    {
      if (iZeroMvdDir == 3)
      {
        uiLastMode = 2;

        pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField( aacAMVPInfo[0][aiZeroMvdRefIdx[0]].m_acMvCand[aiZeroMvdMvpIdx[0]], aiZeroMvdRefIdx[0], ePartSize, uiPartAddr, iPartIdx, 0 );
        pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField( aacAMVPInfo[1][aiZeroMvdRefIdx[1]].m_acMvCand[aiZeroMvdMvpIdx[1]], aiZeroMvdRefIdx[1], ePartSize, uiPartAddr, iPartIdx, 0 );

        pcCU->setInterDirSubParts( 3, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

        pcCU->setMVPIdxSubParts( aiZeroMvdMvpIdx[0], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( aaiMvpNum[0][aiZeroMvdRefIdx[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPIdxSubParts( aiZeroMvdMvpIdx[1], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( aaiMvpNum[1][aiZeroMvdRefIdx[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        uiMEBits = auiZeroMvdBits[2];
      }
      else if (iZeroMvdDir == 1)
      {
        uiLastMode = 0;

        pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField( aacAMVPInfo[0][aiZeroMvdRefIdx[0]].m_acMvCand[aiZeroMvdMvpIdx[0]], aiZeroMvdRefIdx[0], ePartSize, uiPartAddr, iPartIdx, 0 );

        pcCU->setInterDirSubParts( 1, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

        pcCU->setMVPIdxSubParts( aiZeroMvdMvpIdx[0], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( aaiMvpNum[0][aiZeroMvdRefIdx[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        uiMEBits = auiZeroMvdBits[0];
      }
      else if (iZeroMvdDir == 2)
      {
        uiLastMode = 1;

        pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField( aacAMVPInfo[1][aiZeroMvdRefIdx[1]].m_acMvCand[aiZeroMvdMvpIdx[1]], aiZeroMvdRefIdx[1], ePartSize, uiPartAddr, iPartIdx, 0 );

        pcCU->setInterDirSubParts( 2, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

        pcCU->setMVPIdxSubParts( aiZeroMvdMvpIdx[1], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( aaiMvpNum[1][aiZeroMvdRefIdx[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        uiMEBits = auiZeroMvdBits[1];
      }
      else
      {
        assert(0);
      }
    }
    else
#endif
    if ( uiCostBi <= uiCost[0] && uiCostBi <= uiCost[1])
    {
      uiLastMode = 2;
      {
            pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMv( cMvBi[0], ePartSize, uiPartAddr, 0, iPartIdx );
            pcCU->getCUMvField(REF_PIC_LIST_0)->setAllRefIdx( iRefIdxBi[0], ePartSize, uiPartAddr, 0, iPartIdx );
            pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMv( cMvBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
            pcCU->getCUMvField(REF_PIC_LIST_1)->setAllRefIdx( iRefIdxBi[1], ePartSize, uiPartAddr, 0, iPartIdx );
      }
      {
        TempMv = cMvBi[0] - cMvPredBi[0][iRefIdxBi[0]];
            pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );
      }
      {
        TempMv = cMvBi[1] - cMvPredBi[1][iRefIdxBi[1]];
            pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );
      }

      pcCU->setInterDirSubParts( 3, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

      pcCU->setMVPIdxSubParts( aaiMvpIdxBi[0][iRefIdxBi[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[0][iRefIdxBi[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPIdxSubParts( aaiMvpIdxBi[1][iRefIdxBi[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[1][iRefIdxBi[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

      uiMEBits = uiBits[2];
    }
    else if ( uiCost[0] <= uiCost[1] )
    {
      uiLastMode = 0;
          pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMv( cMv[0], ePartSize, uiPartAddr, 0, iPartIdx );
          pcCU->getCUMvField(REF_PIC_LIST_0)->setAllRefIdx( iRefIdx[0], ePartSize, uiPartAddr, 0, iPartIdx );
      {
        TempMv = cMv[0] - cMvPred[0][iRefIdx[0]];
            pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );
      }
      pcCU->setInterDirSubParts( 1, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

      pcCU->setMVPIdxSubParts( aaiMvpIdx[0][iRefIdx[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[0][iRefIdx[0]], REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

      uiMEBits = uiBits[0];
    }
    else
    {
      uiLastMode = 1;
          pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMv( cMv[1], ePartSize, uiPartAddr, 0, iPartIdx );
          pcCU->getCUMvField(REF_PIC_LIST_1)->setAllRefIdx( iRefIdx[1], ePartSize, uiPartAddr, 0, iPartIdx );
      {
        TempMv = cMv[1] - cMvPred[1][iRefIdx[1]];
            pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( TempMv,                 ePartSize, uiPartAddr, 0, iPartIdx );
      }
      pcCU->setInterDirSubParts( 2, uiPartAddr, iPartIdx, pcCU->getDepth(0) );

      pcCU->setMVPIdxSubParts( aaiMvpIdx[1][iRefIdx[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      pcCU->setMVPNumSubParts( aaiMvpNum[1][iRefIdx[1]], REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));

      uiMEBits = uiBits[1];
    }
#if AMP_MRG
    } // end if bTestNormalMC
#endif

    if ( pcCU->getPartitionSize( uiPartAddr ) != SIZE_2Nx2N )
    {
      UInt uiMRGInterDir = 0;
      TComMvField cMRGMvField[2];
      UInt uiMRGIndex = 0;

      UInt uiMEInterDir = 0;
      TComMvField cMEMvField[2];

#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
      m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
      m_pcRdCost->getMotionCost( 1, 0 );
#endif
#if AMP_MRG
      // calculate ME cost
      Distortion uiMEError = std::numeric_limits<Distortion>::max();
      Distortion uiMECost  = std::numeric_limits<Distortion>::max();

      if (bTestNormalMC)
      {
        xGetInterPredictionError( pcCU, pcOrgYuv, iPartIdx, uiMEError, m_pcEncCfg->getUseHADME() );
        uiMECost = uiMEError + m_pcRdCost->getCost( uiMEBits );
      }
#else
      // calculate ME cost
      Distortion uiMEError = std::numeric_limits<Distortion>::max();
      xGetInterPredictionError( pcCU, pcOrgYuv, iPartIdx, uiMEError, m_pcEncCfg->getUseHADME() );
      Distortion uiMECost = uiMEError + m_pcRdCost->getCost( uiMEBits );
#endif
      // save ME result.
      uiMEInterDir = pcCU->getInterDir( uiPartAddr );
      pcCU->getMvField( pcCU, uiPartAddr, REF_PIC_LIST_0, cMEMvField[0] );
      pcCU->getMvField( pcCU, uiPartAddr, REF_PIC_LIST_1, cMEMvField[1] );

      // find Merge result
      Distortion uiMRGCost = std::numeric_limits<Distortion>::max();

      xMergeEstimation( pcCU, pcOrgYuv, iPartIdx, uiMRGInterDir, cMRGMvField, uiMRGIndex, uiMRGCost, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand);

      if ( uiMRGCost < uiMECost )
      {
        // set Merge result
        pcCU->setMergeFlagSubParts ( true,          uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->setMergeIndexSubParts( uiMRGIndex,    uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->setInterDirSubParts  ( uiMRGInterDir, uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        {
          pcCU->getCUMvField( REF_PIC_LIST_0 )->setAllMvField( cMRGMvField[0], ePartSize, uiPartAddr, 0, iPartIdx );
          pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMvField( cMRGMvField[1], ePartSize, uiPartAddr, 0, iPartIdx );
        }

        pcCU->getCUMvField(REF_PIC_LIST_0)->setAllMvd    ( cMvZero,            ePartSize, uiPartAddr, 0, iPartIdx );
        pcCU->getCUMvField(REF_PIC_LIST_1)->setAllMvd    ( cMvZero,            ePartSize, uiPartAddr, 0, iPartIdx );

        pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_0, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPIdxSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
        pcCU->setMVPNumSubParts( -1, REF_PIC_LIST_1, uiPartAddr, iPartIdx, pcCU->getDepth(uiPartAddr));
      }
      else
      {
        // set ME result
        pcCU->setMergeFlagSubParts( false,        uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        pcCU->setInterDirSubParts ( uiMEInterDir, uiPartAddr, iPartIdx, pcCU->getDepth( uiPartAddr ) );
        {
          pcCU->getCUMvField( REF_PIC_LIST_0 )->setAllMvField( cMEMvField[0], ePartSize, uiPartAddr, 0, iPartIdx );
          pcCU->getCUMvField( REF_PIC_LIST_1 )->setAllMvField( cMEMvField[1], ePartSize, uiPartAddr, 0, iPartIdx );
        }
      }
    }

    //  MC
    motionCompensation ( pcCU, rpcPredYuv, REF_PIC_LIST_X, iPartIdx );

  } //  end of for ( Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++ )

  setWpScalingDistParam( pcCU, -1, REF_PIC_LIST_X );

  return;
}


#if RExt__N0256_INTRA_BLOCK_COPY

// based on predInterSearch()
Bool TEncSearch::predIntraBCSearch( TComDataCU* pcCU, TComYuv* pcOrgYuv, TComYuv*& rpcPredYuv, TComYuv*& rpcResiYuv, TComYuv*& rpcRecoYuv DEBUG_STRING_FN_DECLARE(sDebug), Bool bUseRes )
{
  rpcPredYuv->clear();
  if ( !bUseRes )
  {
    rpcResiYuv->clear();
  }
  rpcRecoYuv->clear();
  
  TComMvField  cMEMvField;

  TComMv       cZeroMv(0,0);
  TComMv       cMv, cMvd;
  TComMv       cMvPred    = cZeroMv;
  Distortion   uiCost;
  UInt         uiBits     = 0;
  Int          iPartIdx   = 0;
  Int          uiPartAddr = 0;
  PartSize     ePartSize  = pcCU->getPartitionSize( 0 );

#if INTRABC_FASTME
  if(pcCU->getWidth(0) > 16)
    return false;
#endif

  xIntraBlockCopyEstimation ( pcCU, pcOrgYuv, iPartIdx, &cMvPred, cMv, uiBits, uiCost );

  // store intra BV in REF_PIC_LIST_0
  cMEMvField.setMvField( cMv, REF_PIC_LIST_INTRABC);
  pcCU->getCUMvField( REF_PIC_LIST_INTRABC )->setAllMvField( cMEMvField, ePartSize, uiPartAddr, 0, iPartIdx );

  cMvd.setHor(cMv.getHor());
  cMvd.setVer(cMv.getVer());
  pcCU->getCUMvField(REF_PIC_LIST_INTRABC )->setAllMvd(cMvd, ePartSize, uiPartAddr, 0, iPartIdx);

  // no valid intra BV
  if (cMv.getHor() == 0 && cMv.getVer() == 0)
  {
    return false;
  }
   
  // motion compensation
  intraBlockCopy ( pcCU, rpcPredYuv, iPartIdx );  

  return true;
}

// based on xMotionEstimation
Void TEncSearch::xIntraBlockCopyEstimation( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPartIdx, TComMv* pcMvPred, TComMv& rcMv, UInt& ruiBits, Distortion& ruiCost )
{
  UInt          uiPartAddr;
  Int           iRoiWidth;
  Int           iRoiHeight;
  
  TComMv        cMvSrchRngLT;
  TComMv        cMvSrchRngRB;

  TComYuv*      pcYuv = pcYuvOrg;  

  TComPattern   tmpPattern;
  TComPattern*  pcPatternKey  = &tmpPattern;

  Double        fWeight       = 1.0;

  pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iRoiWidth, iRoiHeight ); 

  //  Search key pattern initialization
  pcPatternKey->initPattern( pcYuv->getAddr  ( COMPONENT_Y, uiPartAddr ),
                             iRoiWidth,
                             iRoiHeight,
                             pcYuv->getStride(COMPONENT_Y) );
    
  Pel*        piRefY      = pcCU->getPic()->getPicYuvRec()->getAddr( COMPONENT_Y, pcCU->getAddr(), pcCU->getZorderIdxInCU() + uiPartAddr );
  Int         iRefStride  = pcCU->getPic()->getPicYuvRec()->getStride(COMPONENT_Y);

  TComMv      cMvPred = *pcMvPred;
  
  // assume that intra BV is integer-pel precision
  xSetIntraSearchRange   ( pcCU, cMvPred, iRoiWidth, iRoiHeight, cMvSrchRngLT, cMvSrchRngRB );

  // disable weighted prediction
  setWpScalingDistParam( pcCU, -1, REF_PIC_LIST_X );

  TComMv        ZeroMv(0,0);

#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
  m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
  m_pcRdCost->getMotionCost( 1, 0 );
#endif
  m_pcRdCost->setPredictor(ZeroMv);
  m_pcRdCost->setCostScale  ( 0 );
  
  //  Do integer search  
  xIntraPatternSearch      ( pcCU, pcPatternKey, piRefY, iRefStride, &cMvSrchRngLT, &cMvSrchRngRB, rcMv, ruiCost, iRoiWidth, iRoiHeight );  

  //printf("ruiCost = %d\n", ruiCost);

  UInt uiMvBits = m_pcRdCost->getBits( rcMv.getHor(), rcMv.getVer() );

  ruiBits      += uiMvBits;
  ruiCost       = (Distortion)( floor( fWeight * ( (Double)ruiCost - (Double)m_pcRdCost->getCost( uiMvBits ) ) ) + (Double)m_pcRdCost->getCost( ruiBits ) );
}

// based on xSetSearchRange
Void TEncSearch::xSetIntraSearchRange ( TComDataCU* pcCU, TComMv& cMvPred, Int iRoiWidth, Int iRoiHeight, TComMv& rcMvSrchRngLT, TComMv& rcMvSrchRngRB )
{  
  TComMv cTmpMvPred = cMvPred;
  pcCU->clipMv( cTmpMvPred );

  Int srLeft, srRight, srTop, srBottom;
  
  UInt lcuWidth = pcCU->getSlice()->getSPS()->getMaxCUWidth();
  UInt cuPelX   = pcCU->getCUPelX();
  UInt lcuHeight = pcCU->getSlice()->getSPS()->getMaxCUHeight();
  UInt cuPelY    = pcCU->getCUPelY();

  Int maxXsr;
  if ( (pcCU->getCULeft()==NULL ||
        pcCU->getCULeft()->getSlice()==NULL ||
          (pcCU->getCULeft()->getSCUAddr()+g_auiRasterToZscan[pcCU->getPic()->getNumPartInWidth() - 1 ]
             < pcCU->getPic()->getCU( pcCU->getAddr() )->getSliceStartCU(0) )
       )
      ||
       (pcCU->getCULeft()==NULL ||
        pcCU->getCULeft()->getSlice()==NULL ||
          (pcCU->getPic()->getPicSym()->getTileIdxMap( pcCU->getCULeft()->getAddr() )
             != pcCU->getPic()->getPicSym()->getTileIdxMap(pcCU->getAddr())
          )
       )
     )
  {
    maxXsr    = cuPelX % lcuWidth;
  }
  else
  {
    maxXsr    = (cuPelX % lcuWidth) + std::min<UInt>(lcuWidth, INTRABC_LEFTWIDTH);
  }

  Int maxYsr    = cuPelY % lcuHeight;

  srLeft   = -maxXsr;
  srTop    = -maxYsr;

  srRight = lcuWidth - cuPelX %lcuWidth - iRoiWidth;
  srBottom = lcuHeight - cuPelY % lcuHeight - iRoiHeight;

  rcMvSrchRngLT.setHor( srLeft );
  rcMvSrchRngLT.setVer( srTop );
  rcMvSrchRngRB.setHor( srRight );
  rcMvSrchRngRB.setVer( srBottom );

  pcCU->clipMv        ( rcMvSrchRngLT );
  pcCU->clipMv        ( rcMvSrchRngRB ); 
}

// based on xPatternSearch
Void TEncSearch::xIntraPatternSearch( TComDataCU*   pcCU, TComPattern* pcPatternKey, Pel* piRefY, Int iRefStride, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB, TComMv& rcMv, Distortion& ruiSAD, Int iRoiWidth, Int iRoiHeight)
{
  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  UInt  lcuWidth  = pcCU->getSlice()->getSPS()->getMaxCUWidth();
  UInt  lcuHeight = pcCU->getSlice()->getSPS()->getMaxCUHeight();
  Int  cuPelX     = pcCU->getCUPelX();
  Int  cuPelY     = pcCU->getCUPelY();

  Distortion  uiSad;
  Distortion  uiSadBest = std::numeric_limits<Distortion>::max();
  Int         iBestX    = 0;
  Int         iBestY    = 0;

  Pel*  piRefSrch;

  //-- jclee for using the SAD function pointer
  m_pcRdCost->setDistParam( pcPatternKey, piRefY, iRefStride,  m_cDistParam );

#if INTRABC_FASTME
  setDistParamComp(COMPONENT_Y);
  m_cDistParam.bitDepth  = g_bitDepth[CHANNEL_TYPE_LUMA];
  m_cDistParam.iRows     = 4;//to calculate the sad line by line;
  m_cDistParam.iSubShift = 0;

  Int        iRelCUPelX    = cuPelX % lcuWidth;
  Int        iRelCUPelY    = cuPelY % lcuHeight;
  Distortion uiTempSadBest = 0;
  
     
  for(Int y = max(iSrchRngVerTop, -cuPelY); y <= -iRoiHeight; y++)
  {
    uiSad = m_pcRdCost->getCost( 0, y);   
    
    for(int r = 0; r < iRoiHeight; )
    {
      piRefSrch = piRefY + y * iRefStride + r*iRefStride;
      m_cDistParam.pCur = piRefSrch;
      m_cDistParam.pOrg = pcPatternKey->getROIY() + r * pcPatternKey->getPatternLStride();
       
      uiSad += m_cDistParam.DistFunc( &m_cDistParam );
      if(uiSad > uiSadBest)
        break;

      r += 4;
    }
    
    if ( uiSad < uiSadBest )
    {
      uiSadBest = uiSad;
      iBestX    = 0;
      iBestY    = y;
      uiTempSadBest = uiSad;

      if(uiSadBest <= 3)
      {
        rcMv.set( iBestX, iBestY );  
        ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
        return;     
      }          
    }            
 }    

  for(Int x = -iRoiWidth; x >= max(iSrchRngHorLeft, - cuPelX); x-- )
  {
    uiSad = m_pcRdCost->getCost( x, 0);  
    
    for(int r = 0; r < iRoiHeight; )
    {
      piRefSrch = piRefY + r*iRefStride + x;
      m_cDistParam.pCur = piRefSrch;
      m_cDistParam.pOrg = pcPatternKey->getROIY() + r * pcPatternKey->getPatternLStride();
     
      uiSad += m_cDistParam.DistFunc( &m_cDistParam );
      if(uiSad > uiSadBest)
        break;
  
      r += 4;
    }
    
    if ( uiSad < uiSadBest )
    {
      uiSadBest = uiSad;
      iBestX    = x;
      iBestY    = 0;
      uiTempSadBest = uiSad;
      if(uiSadBest <= 3)
      {
        rcMv.set( iBestX, iBestY );
        ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);    
         return;    
      }       
    }         
  }

  if((!iBestX && !iBestY))
  {
    rcMv.set( iBestX, iBestY );
    ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
    return;
  }

  if(uiSadBest - m_pcRdCost->getCost( iBestX, iBestY) <= 32)
  {
    rcMv.set( iBestX, iBestY );
    ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
    return;    
  }
  
  if(iRoiWidth == 8)
  {    
    Int iPicWidth = pcCU->getSlice()->getSPS()->getPicWidthInLumaSamples();
    Int iPicHeight = pcCU->getSlice()->getSPS()->getPicHeightInLumaSamples();

    for(Int y = max(iSrchRngVerTop, -cuPelY); y <= iSrchRngVerBottom; y +=2)
    {
      if ((y == 0) || ((Int) (cuPelY + y + iRoiHeight) >= iPicHeight))
        continue;

      Int iTempY = y + iRelCUPelY + iRoiHeight - 1;
    
      for(Int x = max(iSrchRngHorLeft, -cuPelX); x <= iSrchRngHorRight; x++)
      {
        if ((x == 0) || ((Int) (cuPelX + x + iRoiWidth) >= iPicWidth))
          continue;

        Int iTempX = x + iRelCUPelX + iRoiWidth - 1;

        if ((iTempX >= 0) && (iTempY >= 0))
        {
          Int iTempRasterIdx = (iTempY/pcCU->getPic()->getMinCUHeight()) * pcCU->getPic()->getNumPartInWidth() + (iTempX/pcCU->getPic()->getMinCUWidth());
          Int iTempZscanIdx = g_auiRasterToZscan[iTempRasterIdx];
          if(iTempZscanIdx >= pcCU->getZorderIdxInCU())
          continue;
        }

        uiSad = m_pcRdCost->getCost( x, y);   
    
        for(int r = 0; r < iRoiHeight; )
        {
          piRefSrch = piRefY + y * iRefStride + r*iRefStride + x;
          m_cDistParam.pCur = piRefSrch;
          m_cDistParam.pOrg = pcPatternKey->getROIY() + r * pcPatternKey->getPatternLStride();
       
          uiSad += m_cDistParam.DistFunc( &m_cDistParam );
          if(uiSad > uiSadBest)
            break;
          r += 4;
        }
    
        if ( uiSad < uiSadBest )
        {
          uiSadBest = uiSad;
          iBestX    = x;
          iBestY    = y;
        }            
      }
    }

    if(uiSadBest - m_pcRdCost->getCost( iBestX, iBestY) <= 16)
    {
      rcMv.set( iBestX, iBestY );
      ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
      return;    
    }
  
    for(Int y = (max(iSrchRngVerTop, -cuPelY) + 1); y <= iSrchRngVerBottom; y += 2)
    {
      if ((y == 0) || ((Int) (cuPelY + y + iRoiHeight) >= iPicHeight))
        continue;

      Int iTempY = y + iRelCUPelY + iRoiHeight - 1;
    
      for(Int x = max(iSrchRngHorLeft, -cuPelX); x <= iSrchRngHorRight; x += 2)
      {
        if ((x == 0) || ((Int) (cuPelX + x + iRoiWidth) >= iPicWidth))
          continue;

        Int iTempX = x + iRelCUPelX + iRoiWidth - 1;

        if ((iTempX >= 0) && (iTempY >= 0))
        {
          Int iTempRasterIdx = (iTempY/pcCU->getPic()->getMinCUHeight()) * pcCU->getPic()->getNumPartInWidth() + (iTempX/pcCU->getPic()->getMinCUWidth());
          Int iTempZscanIdx = g_auiRasterToZscan[iTempRasterIdx];
          if(iTempZscanIdx >= pcCU->getZorderIdxInCU())
          continue;
        }

        uiSad = m_pcRdCost->getCost( x, y);   
    
        for(int r = 0; r < iRoiHeight; )
        {
          piRefSrch = piRefY + y * iRefStride + r*iRefStride + x;
          m_cDistParam.pCur = piRefSrch;
          m_cDistParam.pOrg = pcPatternKey->getROIY() + r * pcPatternKey->getPatternLStride();
       
          uiSad += m_cDistParam.DistFunc( &m_cDistParam );
          if(uiSad > uiSadBest)
            break;

          r += 4;
        }
    
        if ( uiSad < uiSadBest )
        {
          uiSadBest = uiSad;
          iBestX    = x;
          iBestY    = y;
          if(uiSadBest <= 5)
          {
            rcMv.set( iBestX, iBestY );
            ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
            return;     
          }      
        }            
      }
    }        

    if(uiSadBest >= uiTempSadBest)
    {
      rcMv.set( iBestX, iBestY );
      ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
      return;
    }

    if((uiSadBest - m_pcRdCost->getCost( iBestX, iBestY)) <= 32)
    {
      rcMv.set( iBestX, iBestY );
      ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
      return;    
    }    

    uiTempSadBest = uiSadBest;
  
    for(Int y = (max(iSrchRngVerTop, -cuPelY) + 1); y <= iSrchRngVerBottom; y += 2)
    {
      if ((y == 0) || ((Int) (cuPelY + y + iRoiHeight) >= iPicHeight))
        continue;

      Int iTempY = y + iRelCUPelY + iRoiHeight - 1;
    
      for(Int x = (max(iSrchRngHorLeft, -cuPelX) + 1); x <= iSrchRngHorRight; x += 2)
      {

        if ((x == 0) || ((Int) (cuPelX + x + iRoiWidth) >= iPicWidth))
          continue;

        Int iTempX = x + iRelCUPelX + iRoiWidth - 1;

        if ((iTempX >= 0) && (iTempY >= 0))
        {
          Int iTempRasterIdx = (iTempY/pcCU->getPic()->getMinCUHeight()) * pcCU->getPic()->getNumPartInWidth() + (iTempX/pcCU->getPic()->getMinCUWidth());
          Int iTempZscanIdx = g_auiRasterToZscan[iTempRasterIdx];
          if(iTempZscanIdx >= pcCU->getZorderIdxInCU())
            continue;
        }

        uiSad = m_pcRdCost->getCost( x, y);   
    
        for(int r = 0; r < iRoiHeight; )
        {
          piRefSrch = piRefY + y * iRefStride + r*iRefStride + x;
          m_cDistParam.pCur = piRefSrch;
          m_cDistParam.pOrg = pcPatternKey->getROIY() + r * pcPatternKey->getPatternLStride();
       
          uiSad += m_cDistParam.DistFunc( &m_cDistParam );
          if(uiSad > uiSadBest)
            break;

          r += 4;
        }
    
        if ( uiSad < uiSadBest )
        {
          uiSadBest = uiSad;
          iBestX    = x;
          iBestY    = y;      
          if(uiSadBest <= 5)
          {
            rcMv.set( iBestX, iBestY );
            ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);
            return;     
          }      
        }            
      }
    }
  }    
#else
  setDistParamComp(COMPONENT_Y);
  piRefY += (iSrchRngVerBottom * iRefStride);
  Int iPicWidth = pcCU->getSlice()->getSPS()->getPicWidthInLumaSamples();
  Int iPicHeight = pcCU->getSlice()->getSPS()->getPicHeightInLumaSamples();
  
  for(Int y = iSrchRngVerBottom; y >= iSrchRngVerTop; y--)
  {
    if ( ((Int)(cuPelY + y) < 0) || ((Int) (cuPelY + y + iRoiHeight) >= iPicHeight))
    {
      piRefY -= iRefStride;
      continue;
    }

  for(Int x = iSrchRngHorLeft; x <= iSrchRngHorRight; x++ )
  {

      if (((Int)(cuPelX + x) < 0) || ((Int) (cuPelX + x + iRoiWidth) >= iPicWidth))
      {
        continue;
      }

        
      Int iTempX = x + (cuPelX%lcuWidth) + iRoiWidth - 1;
      Int iTempY = y + (cuPelY%lcuHeight) + iRoiHeight - 1;
      if ((iTempX >= 0) && (iTempY >= 0))
      {
      Int iTempRasterIdx = (iTempY/pcCU->getPic()->getMinCUHeight()) * pcCU->getPic()->getNumPartInWidth() + (iTempX/pcCU->getPic()->getMinCUWidth());
        Int iTempZscanIdx = g_auiRasterToZscan[iTempRasterIdx];
        if(iTempZscanIdx >= pcCU->getZorderIdxInCU())
          continue;
      }

      piRefSrch = piRefY + x;
      m_cDistParam.pCur = piRefSrch;
    
      m_cDistParam.bitDepth = g_bitDepth[CHANNEL_TYPE_LUMA];
      uiSad = m_cDistParam.DistFunc( &m_cDistParam );
    
      uiSad += m_pcRdCost->getCost( x, y);

      if ( uiSad < uiSadBest )
      {
        uiSadBest = uiSad;
        iBestX    = x;
        iBestY    = y;
      }        
    }

    piRefY -= iRefStride;
  }    
#endif

  rcMv.set( iBestX, iBestY );

  ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY);

  return;
}
#endif


// AMVP
#if ZERO_MVD_EST
Void TEncSearch::xEstimateMvPredAMVP( TComDataCU* pcCU, TComYuv* pcOrgYuv, UInt uiPartIdx, RefPicList eRefPicList, Int iRefIdx, TComMv& rcMvPred, Bool bFilled, Distortion* puiDistBiP, Distortion* puiDist  )
#else
Void TEncSearch::xEstimateMvPredAMVP( TComDataCU* pcCU, TComYuv* pcOrgYuv, UInt uiPartIdx, RefPicList eRefPicList, Int iRefIdx, TComMv& rcMvPred, Bool bFilled, Distortion* puiDistBiP )
#endif
{
  AMVPInfo*  pcAMVPInfo = pcCU->getCUMvField(eRefPicList)->getAMVPInfo();

  TComMv     cBestMv;
  Int        iBestIdx   = 0;
  TComMv     cZeroMv;
  TComMv     cMvPred;
  Distortion uiBestCost = std::numeric_limits<Distortion>::max();
  UInt       uiPartAddr = 0;
  Int        iRoiWidth, iRoiHeight;
  Int        i;

  pcCU->getPartIndexAndSize( uiPartIdx, uiPartAddr, iRoiWidth, iRoiHeight );
  // Fill the MV Candidates
  if (!bFilled)
  {
    pcCU->fillMvpCand( uiPartIdx, uiPartAddr, eRefPicList, iRefIdx, pcAMVPInfo );
  }

  // initialize Mvp index & Mvp
  iBestIdx = 0;
  cBestMv  = pcAMVPInfo->m_acMvCand[0];
#if !ZERO_MVD_EST
  if (pcAMVPInfo->iN <= 1)
  {
    rcMvPred = cBestMv;

    pcCU->setMVPIdxSubParts( iBestIdx, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
    pcCU->setMVPNumSubParts( pcAMVPInfo->iN, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));

    if(pcCU->getSlice()->getMvdL1ZeroFlag() && eRefPicList==REF_PIC_LIST_1)
    {
      (*puiDistBiP) = xGetTemplateCost( pcCU, uiPartIdx, uiPartAddr, pcOrgYuv, &m_cYuvPredTemp, rcMvPred, 0, AMVP_MAX_NUM_CANDS, eRefPicList, iRefIdx, iRoiWidth, iRoiHeight);
    }
    return;
  }
#endif

  if (bFilled)
  {
    assert(pcCU->getMVPIdx(eRefPicList,uiPartAddr) >= 0);
    rcMvPred = pcAMVPInfo->m_acMvCand[pcCU->getMVPIdx(eRefPicList,uiPartAddr)];
    return;
  }

  m_cYuvPredTemp.clear();
#if ZERO_MVD_EST
  Distortion uiDist;
#endif
  //-- Check Minimum Cost.
  for ( i = 0 ; i < pcAMVPInfo->iN; i++)
  {
    Distortion uiTmpCost;
#if ZERO_MVD_EST
    uiTmpCost = xGetTemplateCost( pcCU, uiPartIdx, uiPartAddr, pcOrgYuv, &m_cYuvPredTemp, pcAMVPInfo->m_acMvCand[i], i, AMVP_MAX_NUM_CANDS, eRefPicList, iRefIdx, iRoiWidth, iRoiHeight, uiDist );
#else
    uiTmpCost = xGetTemplateCost( pcCU, uiPartIdx, uiPartAddr, pcOrgYuv, &m_cYuvPredTemp, pcAMVPInfo->m_acMvCand[i], i, AMVP_MAX_NUM_CANDS, eRefPicList, iRefIdx, iRoiWidth, iRoiHeight);
#endif
    if ( uiBestCost > uiTmpCost )
    {
      uiBestCost = uiTmpCost;
      cBestMv   = pcAMVPInfo->m_acMvCand[i];
      iBestIdx  = i;
      (*puiDistBiP) = uiTmpCost;
#if ZERO_MVD_EST
      (*puiDist) = uiDist;
#endif
    }
  }

  m_cYuvPredTemp.clear();

  // Setting Best MVP
  rcMvPred = cBestMv;
  pcCU->setMVPIdxSubParts( iBestIdx, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
  pcCU->setMVPNumSubParts( pcAMVPInfo->iN, eRefPicList, uiPartAddr, uiPartIdx, pcCU->getDepth(uiPartAddr));
  return;
}

UInt TEncSearch::xGetMvpIdxBits(Int iIdx, Int iNum)
{
  assert(iIdx >= 0 && iNum >= 0 && iIdx < iNum);

  if (iNum == 1)
    return 0;

  UInt uiLength = 1;
  Int iTemp = iIdx;
  if ( iTemp == 0 )
  {
    return uiLength;
  }

  Bool bCodeLast = ( iNum-1 > iTemp );

  uiLength += (iTemp-1);

  if( bCodeLast )
  {
    uiLength++;
  }

  return uiLength;
}

Void TEncSearch::xGetBlkBits( PartSize eCUMode, Bool bPSlice, Int iPartIdx, UInt uiLastMode, UInt uiBlkBit[3])
{
  if ( eCUMode == SIZE_2Nx2N )
  {
    uiBlkBit[0] = (! bPSlice) ? 3 : 1;
    uiBlkBit[1] = 3;
    uiBlkBit[2] = 5;
  }
  else if ( (eCUMode == SIZE_2NxN || eCUMode == SIZE_2NxnU) || eCUMode == SIZE_2NxnD )
  {
    UInt aauiMbBits[2][3][3] = { { {0,0,3}, {0,0,0}, {0,0,0} } , { {5,7,7}, {7,5,7}, {9-3,9-3,9-3} } };
    if ( bPSlice )
    {
      uiBlkBit[0] = 3;
      uiBlkBit[1] = 0;
      uiBlkBit[2] = 0;
    }
    else
    {
      ::memcpy( uiBlkBit, aauiMbBits[iPartIdx][uiLastMode], 3*sizeof(UInt) );
    }
  }
  else if ( (eCUMode == SIZE_Nx2N || eCUMode == SIZE_nLx2N) || eCUMode == SIZE_nRx2N )
  {
    UInt aauiMbBits[2][3][3] = { { {0,2,3}, {0,0,0}, {0,0,0} } , { {5,7,7}, {7-2,7-2,9-2}, {9-3,9-3,9-3} } };
    if ( bPSlice )
    {
      uiBlkBit[0] = 3;
      uiBlkBit[1] = 0;
      uiBlkBit[2] = 0;
    }
    else
    {
      ::memcpy( uiBlkBit, aauiMbBits[iPartIdx][uiLastMode], 3*sizeof(UInt) );
    }
  }
  else if ( eCUMode == SIZE_NxN )
  {
    uiBlkBit[0] = (! bPSlice) ? 3 : 1;
    uiBlkBit[1] = 3;
    uiBlkBit[2] = 5;
  }
  else
  {
    printf("Wrong!\n");
    assert( 0 );
  }
}

Void TEncSearch::xCopyAMVPInfo (AMVPInfo* pSrc, AMVPInfo* pDst)
{
  pDst->iN = pSrc->iN;
  for (Int i = 0; i < pSrc->iN; i++)
  {
    pDst->m_acMvCand[i] = pSrc->m_acMvCand[i];
  }
}

Void TEncSearch::xCheckBestMVP ( TComDataCU* pcCU, RefPicList eRefPicList, TComMv cMv, TComMv& rcMvPred, Int& riMVPIdx, UInt& ruiBits, Distortion& ruiCost )
{
  AMVPInfo* pcAMVPInfo = pcCU->getCUMvField(eRefPicList)->getAMVPInfo();

  assert(pcAMVPInfo->m_acMvCand[riMVPIdx] == rcMvPred);

  if (pcAMVPInfo->iN < 2) return;


#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
  m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(0) );
#else
  m_pcRdCost->getMotionCost( 1, 0 );
#endif
  m_pcRdCost->setCostScale ( 0    );

  Int iBestMVPIdx = riMVPIdx;

  m_pcRdCost->setPredictor( rcMvPred );
  Int iOrgMvBits  = m_pcRdCost->getBits(cMv.getHor(), cMv.getVer());
  iOrgMvBits += m_auiMVPIdxCost[riMVPIdx][AMVP_MAX_NUM_CANDS];
  Int iBestMvBits = iOrgMvBits;

  for (Int iMVPIdx = 0; iMVPIdx < pcAMVPInfo->iN; iMVPIdx++)
  {
    if (iMVPIdx == riMVPIdx) continue;

    m_pcRdCost->setPredictor( pcAMVPInfo->m_acMvCand[iMVPIdx] );

    Int iMvBits = m_pcRdCost->getBits(cMv.getHor(), cMv.getVer());
    iMvBits += m_auiMVPIdxCost[iMVPIdx][AMVP_MAX_NUM_CANDS];

    if (iMvBits < iBestMvBits)
    {
      iBestMvBits = iMvBits;
      iBestMVPIdx = iMVPIdx;
    }
  }

  if (iBestMVPIdx != riMVPIdx)  //if changed
  {
    rcMvPred = pcAMVPInfo->m_acMvCand[iBestMVPIdx];

    riMVPIdx = iBestMVPIdx;
    UInt uiOrgBits = ruiBits;
    ruiBits = uiOrgBits - iOrgMvBits + iBestMvBits;
    ruiCost = (ruiCost - m_pcRdCost->getCost( uiOrgBits ))  + m_pcRdCost->getCost( ruiBits );
  }
}


Distortion TEncSearch::xGetTemplateCost( TComDataCU* pcCU,
                                         UInt        uiPartIdx,
                                         UInt        uiPartAddr,
                                         TComYuv*    pcOrgYuv,
                                         TComYuv*    pcTemplateCand,
                                         TComMv      cMvCand,
                                         Int         iMVPIdx,
                                         Int         iMVPNum,
                                         RefPicList  eRefPicList,
                                         Int         iRefIdx,
                                         Int         iSizeX,
                                         Int         iSizeY
                                      #if ZERO_MVD_EST
                                       , Distortion& ruiDist
                                      #endif
                                         )
{
  Distortion uiCost = std::numeric_limits<Distortion>::max();

  TComPicYuv* pcPicYuvRef = pcCU->getSlice()->getRefPic( eRefPicList, iRefIdx )->getPicYuvRec();

  pcCU->clipMv( cMvCand );

  // prediction pattern
  if ( pcCU->getSlice()->getPPS()->getUseWP() && pcCU->getSlice()->getSliceType()==P_SLICE )
  {
    xPredInterBlk( COMPONENT_Y, pcCU, pcPicYuvRef, uiPartAddr, &cMvCand, iSizeX, iSizeY, pcTemplateCand, true );
  }
  else
  {
    xPredInterBlk( COMPONENT_Y, pcCU, pcPicYuvRef, uiPartAddr, &cMvCand, iSizeX, iSizeY, pcTemplateCand, false );
  }

  if ( pcCU->getSlice()->getPPS()->getUseWP() && pcCU->getSlice()->getSliceType()==P_SLICE )
  {
    xWeightedPredictionUni( pcCU, pcTemplateCand, uiPartAddr, iSizeX, iSizeY, eRefPicList, pcTemplateCand, iRefIdx );
  }

  // calc distortion
#if ZERO_MVD_EST
#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
  m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
  m_pcRdCost->getMotionCost( 1, 0 );
#endif
  DistParam cDistParam;
  m_pcRdCost->setDistParam( cDistParam, g_bitDepth[CHANNEL_TYPE_LUMA],
                            pcOrgYuv->getAddr(COMPONENT_Y, uiPartAddr), pcOrgYuv->getStride(COMPONENT_Y),
                            pcTemplateCand->getAddr(COMPONENT_Y, uiPartAddr), pcTemplateCand->getStride(COMPONENT_Y),
#if NS_HAD
                            iSizeX, iSizeY, m_pcEncCfg->getUseHADME(), m_pcEncCfg->getUseNSQT() );
#else
                            iSizeX, iSizeY, m_pcEncCfg->getUseHADME() );
#endif
  ruiDist = cDistParam.DistFunc( &cDistParam );
  uiCost = ruiDist + m_pcRdCost->getCost( m_auiMVPIdxCost[iMVPIdx][iMVPNum] );
#else
#if WEIGHTED_CHROMA_DISTORTION
  uiCost = m_pcRdCost->getDistPart( g_bitDepth[CHANNEL_TYPE_LUMA], pcTemplateCand->getAddr(COMPONENT_Y, uiPartAddr), pcTemplateCand->getStride(COMPONENT_Y), pcOrgYuv->getAddr(COMPONENT_Y, uiPartAddr), pcOrgYuv->getStride(COMPONENT_Y), iSizeX, iSizeY, COMPONENT_Y, DF_SAD );
#else
  uiCost = m_pcRdCost->getDistPart( g_bitDepth[CHANNEL_TYPE_LUMA], pcTemplateCand->getAddr(COMPONENT_Y, uiPartAddr), pcTemplateCand->getStride(COMPONENT_Y), pcOrgYuv->getAddr(COMPONENT_Y, uiPartAddr), pcOrgYuv->getStride(COMPONENT_Y), iSizeX, iSizeY, DF_SAD );
#endif
  uiCost = (UInt) m_pcRdCost->calcRdCost( m_auiMVPIdxCost[iMVPIdx][iMVPNum], uiCost, false, DF_SAD );
#endif
  return uiCost;
}




Void TEncSearch::xMotionEstimation( TComDataCU* pcCU, TComYuv* pcYuvOrg, Int iPartIdx, RefPicList eRefPicList, TComMv* pcMvPred, Int iRefIdxPred, TComMv& rcMv, UInt& ruiBits, Distortion& ruiCost, Bool bBi  )
{
  UInt          uiPartAddr;
  Int           iRoiWidth;
  Int           iRoiHeight;

  TComMv        cMvHalf, cMvQter;
  TComMv        cMvSrchRngLT;
  TComMv        cMvSrchRngRB;

  TComYuv*      pcYuv = pcYuvOrg;

  assert(eRefPicList < MAX_NUM_REF_LIST_ADAPT_SR && iRefIdxPred<Int(MAX_IDX_ADAPT_SR));
  m_iSearchRange = m_aaiAdaptSR[eRefPicList][iRefIdxPred];

  Int           iSrchRng      = ( bBi ? m_bipredSearchRange : m_iSearchRange );
  TComPattern   tmpPattern;
  TComPattern*  pcPatternKey  = &tmpPattern;

  Double        fWeight       = 1.0;

  pcCU->getPartIndexAndSize( iPartIdx, uiPartAddr, iRoiWidth, iRoiHeight );

  if ( bBi )
  {
    TComYuv*  pcYuvOther = &m_acYuvPred[1-(Int)eRefPicList];
    pcYuv                = &m_cYuvPredTemp;

    pcYuvOrg->copyPartToPartYuv( pcYuv, uiPartAddr, iRoiWidth, iRoiHeight );

    pcYuv->removeHighFreq( pcYuvOther, uiPartAddr, iRoiWidth, iRoiHeight );

    fWeight = 0.5;
  }

  //  Search key pattern initialization
  pcPatternKey->initPattern( pcYuv->getAddr  ( COMPONENT_Y, uiPartAddr ),
                             iRoiWidth,
                             iRoiHeight,
                             pcYuv->getStride(COMPONENT_Y) );

  Pel*        piRefY      = pcCU->getSlice()->getRefPic( eRefPicList, iRefIdxPred )->getPicYuvRec()->getAddr( COMPONENT_Y, pcCU->getAddr(), pcCU->getZorderIdxInCU() + uiPartAddr );
  Int         iRefStride  = pcCU->getSlice()->getRefPic( eRefPicList, iRefIdxPred )->getPicYuvRec()->getStride(COMPONENT_Y);

  TComMv      cMvPred = *pcMvPred;

  if ( bBi )  xSetSearchRange   ( pcCU, rcMv   , iSrchRng, cMvSrchRngLT, cMvSrchRngRB );
  else        xSetSearchRange   ( pcCU, cMvPred, iSrchRng, cMvSrchRngLT, cMvSrchRngRB );

#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
  m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
  m_pcRdCost->getMotionCost( 1, 0 );
#endif

  m_pcRdCost->setPredictor  ( *pcMvPred );
  m_pcRdCost->setCostScale  ( 2 );

  setWpScalingDistParam( pcCU, iRefIdxPred, eRefPicList );
  //  Do integer search
  if ( !m_iFastSearch || bBi )
  {
    xPatternSearch      ( pcPatternKey, piRefY, iRefStride, &cMvSrchRngLT, &cMvSrchRngRB, rcMv, ruiCost );
  }
  else
  {
    rcMv = *pcMvPred;
    xPatternSearchFast  ( pcCU, pcPatternKey, piRefY, iRefStride, &cMvSrchRngLT, &cMvSrchRngRB, rcMv, ruiCost );
  }

#if RExt__LOSSLESS_AND_MIXED_LOSSLESS_RD_COST_EVALUATION
  m_pcRdCost->getMotionCost( true, 0, pcCU->getCUTransquantBypass(uiPartAddr) );
#else
  m_pcRdCost->getMotionCost( 1, 0 );
#endif
  m_pcRdCost->setCostScale ( 1 );

#ifdef ZERO_MOTION_VECTORS
  if (0)
#endif
  {
    xPatternSearchFracDIF( pcCU, pcPatternKey, piRefY, iRefStride, &rcMv, cMvHalf, cMvQter, ruiCost ,bBi );
  }



  m_pcRdCost->setCostScale( 0 );
  rcMv <<= 2;

#ifdef ZERO_MOTION_VECTORS
  rcMv.setZero();
#else
  rcMv += (cMvHalf <<= 1);
  rcMv +=  cMvQter;
#endif

  UInt uiMvBits = m_pcRdCost->getBits( rcMv.getHor(), rcMv.getVer() );

  ruiBits      += uiMvBits;
  ruiCost       = (Distortion)( floor( fWeight * ( (Double)ruiCost - (Double)m_pcRdCost->getCost( uiMvBits ) ) ) + (Double)m_pcRdCost->getCost( ruiBits ) );
}




Void TEncSearch::xSetSearchRange ( TComDataCU* pcCU, TComMv& cMvPred, Int iSrchRng, TComMv& rcMvSrchRngLT, TComMv& rcMvSrchRngRB )
{
  Int  iMvShift = 2;
  TComMv cTmpMvPred = cMvPred;
  pcCU->clipMv( cTmpMvPred );

  rcMvSrchRngLT.setHor( cTmpMvPred.getHor() - (iSrchRng << iMvShift) );
  rcMvSrchRngLT.setVer( cTmpMvPred.getVer() - (iSrchRng << iMvShift) );

  rcMvSrchRngRB.setHor( cTmpMvPred.getHor() + (iSrchRng << iMvShift) );
  rcMvSrchRngRB.setVer( cTmpMvPred.getVer() + (iSrchRng << iMvShift) );
  pcCU->clipMv        ( rcMvSrchRngLT );
  pcCU->clipMv        ( rcMvSrchRngRB );

  rcMvSrchRngLT >>= iMvShift;
  rcMvSrchRngRB >>= iMvShift;
}




Void TEncSearch::xPatternSearch( TComPattern* pcPatternKey, Pel* piRefY, Int iRefStride, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB, TComMv& rcMv, Distortion& ruiSAD )
{
  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  Distortion  uiSad;
  Distortion  uiSadBest = std::numeric_limits<Distortion>::max();
  Int         iBestX = 0;
  Int         iBestY = 0;

  Pel*  piRefSrch;

  //-- jclee for using the SAD function pointer
  m_pcRdCost->setDistParam( pcPatternKey, piRefY, iRefStride,  m_cDistParam );

  // fast encoder decision: use subsampled SAD for integer ME
  if ( m_pcEncCfg->getUseFastEnc() )
  {
    if ( m_cDistParam.iRows > 8 )
    {
      m_cDistParam.iSubShift = 1;
    }
  }

  piRefY += (iSrchRngVerTop * iRefStride);
  for ( Int y = iSrchRngVerTop; y <= iSrchRngVerBottom; y++ )
  {
    for ( Int x = iSrchRngHorLeft; x <= iSrchRngHorRight; x++ )
    {
      //  find min. distortion position
      piRefSrch = piRefY + x;
      m_cDistParam.pCur = piRefSrch;

      setDistParamComp(COMPONENT_Y);

      m_cDistParam.bitDepth = g_bitDepth[CHANNEL_TYPE_LUMA];
      uiSad = m_cDistParam.DistFunc( &m_cDistParam );

      // motion cost
      uiSad += m_pcRdCost->getCost( x, y );

      if ( uiSad < uiSadBest )
      {
        uiSadBest = uiSad;
        iBestX    = x;
        iBestY    = y;
      }
    }
    piRefY += iRefStride;
  }

  rcMv.set( iBestX, iBestY );

  ruiSAD = uiSadBest - m_pcRdCost->getCost( iBestX, iBestY );
  return;
}




Void TEncSearch::xPatternSearchFast( TComDataCU* pcCU, TComPattern* pcPatternKey, Pel* piRefY, Int iRefStride, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB, TComMv& rcMv, Distortion& ruiSAD )
{
  assert (MD_LEFT < NUM_MV_PREDICTORS);
  pcCU->getMvPredLeft       ( m_acMvPredictors[MD_LEFT] );
  assert (MD_ABOVE < NUM_MV_PREDICTORS);
  pcCU->getMvPredAbove      ( m_acMvPredictors[MD_ABOVE] );
  assert (MD_ABOVE_RIGHT < NUM_MV_PREDICTORS);
  pcCU->getMvPredAboveRight ( m_acMvPredictors[MD_ABOVE_RIGHT] );

  switch ( m_iFastSearch )
  {
    case 1:
      xTZSearch( pcCU, pcPatternKey, piRefY, iRefStride, pcMvSrchRngLT, pcMvSrchRngRB, rcMv, ruiSAD );
      break;

    default:
      break;
  }
}




Void TEncSearch::xTZSearch( TComDataCU* pcCU, TComPattern* pcPatternKey, Pel* piRefY, Int iRefStride, TComMv* pcMvSrchRngLT, TComMv* pcMvSrchRngRB, TComMv& rcMv, Distortion& ruiSAD )
{
#ifdef ZERO_MOTION_VECTORS
  rcMv.set( 0, 0 );
  ruiSAD = 0;
  return;
#endif

  Int   iSrchRngHorLeft   = pcMvSrchRngLT->getHor();
  Int   iSrchRngHorRight  = pcMvSrchRngRB->getHor();
  Int   iSrchRngVerTop    = pcMvSrchRngLT->getVer();
  Int   iSrchRngVerBottom = pcMvSrchRngRB->getVer();

  TZ_SEARCH_CONFIGURATION

  UInt uiSearchRange = m_iSearchRange;
  pcCU->clipMv( rcMv );
  rcMv >>= 2;
  // init TZSearchStruct
  IntTZSearchStruct cStruct;
  cStruct.iYStride    = iRefStride;
  cStruct.piRefY      = piRefY;
  cStruct.uiBestSad   = MAX_UINT;

  // set rcMv (Median predictor) as start point and as best point
  xTZSearchHelp( pcPatternKey, cStruct, rcMv.getHor(), rcMv.getVer(), 0, 0 );

  // test whether one of PRED_A, PRED_B, PRED_C MV is better start point than Median predictor
  if ( bTestOtherPredictedMV )
  {
    for ( UInt index = 0; index < NUM_MV_PREDICTORS; index++ )
    {
      TComMv cMv = m_acMvPredictors[index];
      pcCU->clipMv( cMv );
      cMv >>= 2;
      xTZSearchHelp( pcPatternKey, cStruct, cMv.getHor(), cMv.getVer(), 0, 0 );
    }
  }

  // test whether zero Mv is better start point than Median predictor
  if ( bTestZeroVector )
  {
    xTZSearchHelp( pcPatternKey, cStruct, 0, 0, 0, 0 );
  }

  // start search
  Int  iDist = 0;
  Int  iStartX = cStruct.iBestX;
  Int  iStartY = cStruct.iBestY;

  // first search
  for ( iDist = 1; iDist <= (Int)uiSearchRange; iDist*=2 )
  {
    if ( bFirstSearchDiamond == 1 )
    {
      xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
    }
    else
    {
      xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
    }

    if ( bFirstSearchStop && ( cStruct.uiBestRound >= uiFirstSearchRounds ) ) // stop criterion
    {
      break;
    }
  }

  // test whether zero Mv is a better start point than Median predictor
  if ( bTestZeroVectorStart && ((cStruct.iBestX != 0) || (cStruct.iBestY != 0)) )
  {
    xTZSearchHelp( pcPatternKey, cStruct, 0, 0, 0, 0 );
    if ( (cStruct.iBestX == 0) && (cStruct.iBestY == 0) )
    {
      // test its neighborhood
      for ( iDist = 1; iDist <= (Int)uiSearchRange; iDist*=2 )
      {
        xTZ8PointDiamondSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, 0, 0, iDist );
        if ( bTestZeroVectorStop && (cStruct.uiBestRound > 0) ) // stop criterion
        {
          break;
        }
      }
    }
  }

  // calculate only 2 missing points instead 8 points if cStruct.uiBestDistance == 1
  if ( cStruct.uiBestDistance == 1 )
  {
    cStruct.uiBestDistance = 0;
    xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
  }

  // raster search if distance is too big
  if ( bEnableRasterSearch && ( ((Int)(cStruct.uiBestDistance) > iRaster) || bAlwaysRasterSearch ) )
  {
    cStruct.uiBestDistance = iRaster;
    for ( iStartY = iSrchRngVerTop; iStartY <= iSrchRngVerBottom; iStartY += iRaster )
    {
      for ( iStartX = iSrchRngHorLeft; iStartX <= iSrchRngHorRight; iStartX += iRaster )
      {
        xTZSearchHelp( pcPatternKey, cStruct, iStartX, iStartY, 0, iRaster );
      }
    }
  }

  // raster refinement
  if ( bRasterRefinementEnable && cStruct.uiBestDistance > 0 )
  {
    while ( cStruct.uiBestDistance > 0 )
    {
      iStartX = cStruct.iBestX;
      iStartY = cStruct.iBestY;
      if ( cStruct.uiBestDistance > 1 )
      {
        iDist = cStruct.uiBestDistance >>= 1;
        if ( bRasterRefinementDiamond == 1 )
        {
          xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
        else
        {
          xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
      }

      // calculate only 2 missing points instead 8 points if cStruct.uiBestDistance == 1
      if ( cStruct.uiBestDistance == 1 )
      {
        cStruct.uiBestDistance = 0;
        if ( cStruct.ucPointNr != 0 )
        {
          xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
        }
      }
    }
  }

  // start refinement
  if ( bStarRefinementEnable && cStruct.uiBestDistance > 0 )
  {
    while ( cStruct.uiBestDistance > 0 )
    {
      iStartX = cStruct.iBestX;
      iStartY = cStruct.iBestY;
      cStruct.uiBestDistance = 0;
      cStruct.ucPointNr = 0;
      for ( iDist = 1; iDist < (Int)uiSearchRange + 1; iDist*=2 )
      {
        if ( bStarRefinementDiamond == 1 )
        {
          xTZ8PointDiamondSearch ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
        else
        {
          xTZ8PointSquareSearch  ( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB, iStartX, iStartY, iDist );
        }
        if ( bStarRefinementStop && (cStruct.uiBestRound >= uiStarRefinementRounds) ) // stop criterion
        {
          break;
        }
      }

      // calculate only 2 missing points instead 8 points if cStrukt.uiBestDistance == 1
      if ( cStruct.uiBestDistance == 1 )
      {
        cStruct.uiBestDistance = 0;
        if ( cStruct.ucPointNr != 0 )
        {
          xTZ2PointSearch( pcPatternKey, cStruct, pcMvSrchRngLT, pcMvSrchRngRB );
        }
      }
    }
  }

  // write out best match
  rcMv.set( cStruct.iBestX, cStruct.iBestY );
  ruiSAD = cStruct.uiBestSad - m_pcRdCost->getCost( cStruct.iBestX, cStruct.iBestY );
}




Void TEncSearch::xPatternSearchFracDIF(TComDataCU*  pcCU,
                                       TComPattern* pcPatternKey,
                                       Pel*         piRefY,
                                       Int          iRefStride,
                                       TComMv*      pcMvInt,
                                       TComMv&      rcMvHalf,
                                       TComMv&      rcMvQter,
                                       Distortion&  ruiCost,
                                       Bool         biPred
                                      )
{
  //  Reference pattern initialization (integer scale)
  TComPattern cPatternRoi;
  Int         iOffset    = pcMvInt->getHor() + pcMvInt->getVer() * iRefStride;
  cPatternRoi.initPattern(piRefY + iOffset,
                          pcPatternKey->getROIYWidth(),
                          pcPatternKey->getROIYHeight(),
                          iRefStride );

  //  Half-pel refinement
  xExtDIFUpSamplingH ( &cPatternRoi, biPred );

  rcMvHalf = *pcMvInt;   rcMvHalf <<= 1;    // for mv-cost
  TComMv baseRefMv(0, 0);
  ruiCost = xPatternRefinement( pcPatternKey, baseRefMv, 2, rcMvHalf   );

  m_pcRdCost->setCostScale( 0 );

  xExtDIFUpSamplingQ ( &cPatternRoi, rcMvHalf, biPred );
  baseRefMv = rcMvHalf;
  baseRefMv <<= 1;

  rcMvQter = *pcMvInt;   rcMvQter <<= 1;    // for mv-cost
  rcMvQter += rcMvHalf;  rcMvQter <<= 1;
  ruiCost = xPatternRefinement( pcPatternKey, baseRefMv, 1, rcMvQter );
}


/** encode residual and calculate rate-distortion for a CU block
 * \param pcCU
 * \param pcYuvOrg
 * \param pcYuvPred
 * \param rpcYuvResi
 * \param rpcYuvResiBest
 * \param rpcYuvRec
 * \param bSkipRes
 * \returns Void
 */
Void TEncSearch::encodeResAndCalcRdInterCU( TComDataCU* pcCU, TComYuv* pcYuvOrg, TComYuv* pcYuvPred,
                                            TComYuv*& rpcYuvResi, TComYuv*& rpcYuvResiBest, TComYuv*& rpcYuvRec,
                                            Bool bSkipRes DEBUG_STRING_FN_DECLARE(sDebug) )
{
  if ( pcCU->isIntra(0) )
  {
    return;
  }

  Bool       bHighPass    = pcCU->getSlice()->getDepth() ? true : false;
  UInt       uiBits       = 0, uiBitsBest       = 0;
  Distortion uiDistortion = 0, uiDistortionBest = 0;

  UInt        uiWidth      = pcCU->getWidth ( 0 );
  UInt        uiHeight     = pcCU->getHeight( 0 );

  //  No residual coding : SKIP mode
  if ( bSkipRes )
  {
    pcCU->setSkipFlagSubParts( true, 0, pcCU->getDepth(0) );

    rpcYuvResi->clear();

    pcYuvPred->copyToPartYuv( rpcYuvRec, 0 );

    for (UInt ch=0; ch < pcCU->getPic()->getNumberValidComponents(); ch++)
    {
      const ComponentID compID=ComponentID(ch);
      const UInt csx=pcYuvOrg->getComponentScaleX(compID);
      const UInt csy=pcYuvOrg->getComponentScaleY(compID);
      uiDistortion += m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], rpcYuvRec->getAddr(compID), rpcYuvRec->getStride(compID), pcYuvOrg->getAddr(compID),
                                               pcYuvOrg->getStride(compID), uiWidth >> csx, uiHeight >> csy
#if WEIGHTED_CHROMA_DISTORTION
                                               , compID
#endif
                                               );
    }

    if( m_bUseSBACRD )
      m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[pcCU->getDepth(0)][CI_CURR_BEST]);

    m_pcEntropyCoder->resetBits();

    if (pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
    {
      m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, 0, true);
    }

    m_pcEntropyCoder->encodeSkipFlag(pcCU, 0, true);
    m_pcEntropyCoder->encodeMergeIndex( pcCU, 0, true );

    uiBits = m_pcEntropyCoder->getNumberOfWrittenBits();
    pcCU->getTotalBits()       = uiBits;
    pcCU->getTotalDistortion() = uiDistortion;
    pcCU->getTotalCost()       = m_pcRdCost->calcRdCost( uiBits, uiDistortion );

    if( m_bUseSBACRD )
      m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[pcCU->getDepth(0)][CI_TEMP_BEST]);

    static const UInt cbfZero[MAX_NUM_COMPONENT]={0,0,0};
    pcCU->setCbfSubParts( cbfZero, 0, pcCU->getDepth( 0 ) );
    pcCU->setTrIdxSubParts( 0, 0, pcCU->getDepth(0) );

    return;
  }

  //  Residual coding.
  Int qp;
  Int qpBest = 0;
  Int qpMin;
  Int qpMax;
  Double  dCost, dCostBest = MAX_DOUBLE;

  UInt uiTrLevel = 0;
  if( (pcCU->getWidth(0) > pcCU->getSlice()->getSPS()->getMaxTrSize()) )
  {
    while( pcCU->getWidth(0) > (pcCU->getSlice()->getSPS()->getMaxTrSize()<<uiTrLevel) ) uiTrLevel++;
  }
  UInt uiMaxTrMode = 1 + uiTrLevel;

  while((uiWidth>>uiMaxTrMode) < (g_uiMaxCUWidth>>g_uiMaxCUDepth)) uiMaxTrMode--;

  qpMin =  bHighPass ? Clip3( -pcCU->getSlice()->getSPS()->getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, pcCU->getQP(0) - m_iMaxDeltaQP ) : pcCU->getQP( 0 );
  qpMax =  bHighPass ? Clip3( -pcCU->getSlice()->getSPS()->getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, pcCU->getQP(0) + m_iMaxDeltaQP ) : pcCU->getQP( 0 );

  rpcYuvResi->subtract( pcYuvOrg, pcYuvPred, 0, uiWidth );

  TComTURecurse tuLevel0(pcCU, 0);

  for ( qp = qpMin; qp <= qpMax; qp++ )
  {
    dCost = 0.;
    uiBits = 0;
    uiDistortion = 0;
    if( m_bUseSBACRD )
    {
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ pcCU->getDepth( 0 ) ][ CI_CURR_BEST ] );
    }

    Distortion uiZeroDistortion = 0;

    xEstimateResidualQT( rpcYuvResi,  dCost, uiBits, uiDistortion, &uiZeroDistortion, tuLevel0 DEBUG_STRING_PASS_INTO(sDebug) );

    // -------------------------------------------------------
    // set the coefficients in the pcCU, and also calculates the residual data.
    // If a block full of 0's is efficient, then just use 0's.
    // The costs at this point do not include header bits.

    m_pcEntropyCoder->resetBits();
    m_pcEntropyCoder->encodeQtRootCbfZero( pcCU );
    UInt zeroResiBits = m_pcEntropyCoder->getNumberOfWrittenBits();
    Double dZeroCost = m_pcRdCost->calcRdCost( zeroResiBits, uiZeroDistortion );

    if(pcCU->isLosslessCoded( 0 ))
    {
      dZeroCost = dCost + 1;
    }

    if ( dZeroCost < dCost )
    {
      dCost        = dZeroCost;
      uiBits       = 0;
      uiDistortion = uiZeroDistortion;

      const UInt uiQPartNum = tuLevel0.GetAbsPartIdxNumParts();
      ::memset( pcCU->getTransformIdx()     , 0, uiQPartNum * sizeof(UChar) );
      for (UInt ch=0; ch < pcCU->getPic()->getNumberValidComponents(); ch++)
      {
        const ComponentID component = ComponentID(ch);
        const UInt componentShift   = pcCU->getPic()->getComponentScaleX(component) + pcCU->getPic()->getComponentScaleY(component);
        ::memset( pcCU->getCbf( component ) , 0, uiQPartNum * sizeof(UChar) );
        ::memset( pcCU->getCoeff(component), 0, (uiWidth*uiHeight*sizeof(TCoeff))>>componentShift );
      }
      static const UInt useTS[MAX_NUM_COMPONENT]={0,0,0};
      pcCU->setTransformSkipSubParts ( useTS, 0, pcCU->getDepth(0) );
    }
#if RExt__N0256_INTRA_BLOCK_COPY
    else if (!pcCU->isLosslessCoded( 0 ) && pcCU->isIntraBC(0) && uiZeroDistortion == uiDistortion)
    {
      const UInt uiQPartNum = tuLevel0.GetAbsPartIdxNumParts();
      ::memset( pcCU->getTransformIdx()     , 0, uiQPartNum * sizeof(UChar) );
      for (UInt ch=0; ch < pcCU->getPic()->getNumberValidComponents(); ch++)
      {
        const ComponentID component = ComponentID(ch);
        const UInt componentShift   = pcCU->getPic()->getComponentScaleX(component) + pcCU->getPic()->getComponentScaleY(component);
        ::memset( pcCU->getCbf( component ) , 0, uiQPartNum * sizeof(UChar) );
        ::memset( pcCU->getCoeff(component), 0, (uiWidth*uiHeight*sizeof(TCoeff))>>componentShift );
      }
      static const UInt useTS[MAX_NUM_COMPONENT]={0,0,0};
      pcCU->setTransformSkipSubParts ( useTS, 0, pcCU->getDepth(0) );
    }
#endif
    else
    {
      xSetResidualQTData( NULL, false, tuLevel0); // Call first time to set coefficients.
    }

    if( m_bUseSBACRD )
    {
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[pcCU->getDepth(0)][CI_CURR_BEST] );
    }

    uiBits = 0;
    {
      TComYuv *pDummy = NULL;
      xAddSymbolBitsInter( pcCU, 0, 0, uiBits, pDummy, NULL, pDummy );
    }
    // we've now encoded the pcCU, and so have a valid bit cost


    Double dExactCost = m_pcRdCost->calcRdCost( uiBits, uiDistortion );
    dCost = dExactCost;

    // Is our new cost better?
    if ( dCost < dCostBest )
    {
      if ( !pcCU->getQtRootCbf( 0 ) )
      {
        rpcYuvResiBest->clear(); // Clear the residual image, if we didn't code it.
      }
      else
      {
        xSetResidualQTData( rpcYuvResiBest, true, tuLevel0 ); // else set the residual image data rpcYUVResiBest from the various temp images.
      }

      if( qpMin != qpMax && qp != qpMax )
      {
        const UInt uiQPartNum = tuLevel0.GetAbsPartIdxNumParts();
        ::memcpy( m_puhQTTempTrIdx, pcCU->getTransformIdx(),        uiQPartNum * sizeof(UChar) );
        for(UInt i=0; i<pcCU->getPic()->getNumberValidComponents(); i++)
        {
          const ComponentID compID=ComponentID(i);
          const UInt csr = pcCU->getPic()->getComponentScaleX(compID) + pcCU->getPic()->getComponentScaleY(compID);
          ::memcpy( m_puhQTTempCbf[compID],      pcCU->getCbf( compID ),     uiQPartNum * sizeof(UChar) );
          ::memcpy( m_pcQTTempCoeff[compID],     pcCU->getCoeff(compID),     uiWidth * uiHeight * sizeof( TCoeff ) >> csr     );
#if ADAPTIVE_QP_SELECTION
          ::memcpy( m_pcQTTempArlCoeff[compID],  pcCU->getArlCoeff(compID),  uiWidth * uiHeight * sizeof( TCoeff )>> csr     );
#endif
          ::memcpy( m_puhQTTempTransformSkipFlag[compID], pcCU->getTransformSkip(compID),     uiQPartNum * sizeof( UChar ) );
        }
      }
      uiBitsBest       = uiBits;
      uiDistortionBest = uiDistortion;
      dCostBest        = dCost;
      qpBest           = qp;
      if( m_bUseSBACRD )
      {
        m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ pcCU->getDepth( 0 ) ][ CI_TEMP_BEST ] );
      }
    }
  }

  assert ( dCostBest != MAX_DOUBLE );

  if( qpMin != qpMax && qpBest != qpMax )
  {
    if( m_bUseSBACRD )
    {
      assert( 0 ); // check
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ pcCU->getDepth( 0 ) ][ CI_TEMP_BEST ] );
    }
    // copy best cbf and trIdx to pcCU
    const UInt uiQPartNum = tuLevel0.GetAbsPartIdxNumParts();
    ::memcpy( pcCU->getTransformIdx(),       m_puhQTTempTrIdx,  uiQPartNum * sizeof(UChar) );
    for(UInt i=0; i<pcCU->getPic()->getNumberValidComponents(); i++)
    {
      const ComponentID compID=ComponentID(i);
      const UInt csr = pcCU->getPic()->getComponentScaleX(compID) + pcCU->getPic()->getComponentScaleY(compID);
      ::memcpy( pcCU->getCbf( compID ),     m_puhQTTempCbf[compID],     uiQPartNum * sizeof(UChar) );
      ::memcpy( pcCU->getCoeff(compID),     m_pcQTTempCoeff[compID],    uiWidth * uiHeight * sizeof( TCoeff ) >> csr     );
#if ADAPTIVE_QP_SELECTION
      ::memcpy( pcCU->getArlCoeff(compID),  m_pcQTTempArlCoeff[compID], uiWidth * uiHeight * sizeof( TCoeff    ) >> csr );
#endif
      ::memcpy( pcCU->getTransformSkip(compID),     m_puhQTTempTransformSkipFlag[compID], uiQPartNum * sizeof( UChar ) );
    }
  }
  rpcYuvRec->addClip ( pcYuvPred, rpcYuvResiBest, 0, uiWidth );

  // update with clipped distortion and cost (qp estimation loop uses unclipped values)

  uiDistortionBest = 0;
  for(UInt ch=0; ch<rpcYuvRec->getNumberValidComponents(); ch++)
  {
    const ComponentID compID=ComponentID(ch);
#if WEIGHTED_CHROMA_DISTORTION
    uiDistortionBest += m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], rpcYuvRec->getAddr(compID ), rpcYuvRec->getStride(compID ), pcYuvOrg->getAddr(compID ), pcYuvOrg->getStride(compID), uiWidth >> pcYuvOrg->getComponentScaleX(compID), uiHeight >> pcYuvOrg->getComponentScaleY(compID), compID);
#else
    uiDistortionBest += m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], rpcYuvRec->getAddr(compID ), rpcYuvRec->getStride(compID ), pcYuvOrg->getAddr(compID ), pcYuvOrg->getStride(compID), uiWidth >> pcYuvOrg->getComponentScaleX(compID), uiHeight >> pcYuvOrg->getComponentScaleY(compID));
#endif
  }
  dCostBest = m_pcRdCost->calcRdCost( uiBitsBest, uiDistortionBest );

  pcCU->getTotalBits()       = uiBitsBest;
  pcCU->getTotalDistortion() = uiDistortionBest;
  pcCU->getTotalCost()       = dCostBest;

  if ( pcCU->isSkipped(0) )
  {
    static const UInt cbfZero[MAX_NUM_COMPONENT]={0,0,0};
    pcCU->setCbfSubParts( cbfZero, 0, pcCU->getDepth( 0 ) );
  }

  pcCU->setQPSubParts( qpBest, 0, pcCU->getDepth(0) );
}



Void TEncSearch::xEstimateResidualQT( TComYuv    *pcResi,
                                      Double     &rdCost,
                                      UInt       &ruiBits,
                                      Distortion &ruiDist,
                                      Distortion *puiZeroDist,
                                      TComTU     &rTu
                                      DEBUG_STRING_FN_DECLARE(sDebug) )
{
  //NOTE: RExt - Ideally this function would be restructured to use just one component loop, but it is kept in this form to maintain HM-compatibility for 4:2:0

  TComDataCU *pcCU        = rTu.getCU();
  const UInt uiAbsPartIdx = rTu.GetAbsPartIdxTU();
  const UInt uiDepth      = rTu.GetTransformDepthTotal();
  const UInt uiTrMode     = rTu.GetTransformDepthRel();
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
  const UInt subTUDepth   = uiTrMode + 1;
#endif
  const UInt numValidComp = pcCU->getPic()->getNumberValidComponents();
  DEBUG_STRING_NEW(sSingleStringComp[MAX_NUM_COMPONENT])

  assert( pcCU->getDepth( 0 ) == pcCU->getDepth( uiAbsPartIdx ) );
  const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();

#if RExt__N0256_INTRA_BLOCK_COPY
  UInt SplitFlag = ((pcCU->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1) && pcCU->isInter(uiAbsPartIdx) && ( pcCU->getPartitionSize(uiAbsPartIdx) != SIZE_2Nx2N ));
#else
  UInt SplitFlag = ((pcCU->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1) && pcCU->getPredictionMode(uiAbsPartIdx) == MODE_INTER && ( pcCU->getPartitionSize(uiAbsPartIdx) != SIZE_2Nx2N ));
#endif
  Bool bCheckFull;
  if ( SplitFlag && uiDepth == pcCU->getDepth(uiAbsPartIdx) && ( uiLog2TrSize >  pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) ) )
     bCheckFull = false;
  else
     bCheckFull =  ( uiLog2TrSize <= pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() );

  const Bool bCheckSplit  = ( uiLog2TrSize >  pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );

  assert( bCheckFull || bCheckSplit );

  // code full block
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
  Double     dSingleCost = MAX_DOUBLE;
  UInt       uiSingleBitsComp   [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
  UInt       uiSingleBits                                                                                               = 0;
  Distortion uiSingleDistComp   [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
  Distortion uiSingleDist                                                                                               = 0;
  TCoeff     uiAbsSum           [MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
  UInt       uiBestTransformMode[MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{0,0},{0,0},{0,0}};
#if RExt__NRCE2_RESIDUAL_DPCM
  //  Stores the best inter RDPCM mode for a TU encoded without split
  UInt bestInterRdpcmModeUnSplit[MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/] = {{3,3}, {3,3}, {3,3}};
#endif
#else
  Double     dSingleCost = MAX_DOUBLE;
  UInt       uiSingleBitsComp[MAX_NUM_COMPONENT] = {0,0,0};
  UInt       uiSingleBits = 0;
  Distortion uiSingleDistComp[MAX_NUM_COMPONENT] = {0,0,0};
  Distortion uiSingleDist = 0;
  TCoeff     uiAbsSum[MAX_NUM_COMPONENT] = {0,0,0};
  UInt       uiBestTransformMode[MAX_NUM_COMPONENT] = {0,0,0};
#if RExt__NRCE2_RESIDUAL_DPCM
  //  Stores the best inter RDPCM mode for a TU encoded without split
  UInt bestInterRdpcmModeUnSplit[MAX_NUM_COMPONENT] = {0,0,0};
#endif
#endif

  if( m_bUseSBACRD )
  {
    m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
  }

  if( bCheckFull )
  {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
    Double minCost[MAX_NUM_COMPONENT][2/*0 = top (or whole TU for non-4:2:2) sub-TU, 1 = bottom sub-TU*/];
#else
    Double minCost[MAX_NUM_COMPONENT];
#endif
    Bool checkTransformSkip[MAX_NUM_COMPONENT];
    pcCU->setTrIdxSubParts( uiTrMode, uiAbsPartIdx, uiDepth );

    m_pcEntropyCoder->resetBits();

    UInt uiSingleBitsPrev=0;

    memset( m_pTempPel, 0, sizeof( Pel ) * rTu.getRect(COMPONENT_Y).width * rTu.getRect(COMPONENT_Y).height ); // not necessary needed for inside of recursion (only at the beginning)

    const UInt uiQTTempAccessLayer = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
    TCoeff *pcCoeffCurr[MAX_NUM_COMPONENT];
#if ADAPTIVE_QP_SELECTION
    TCoeff *pcArlCoeffCurr[MAX_NUM_COMPONENT];
#endif

    for(UInt i=0; i<numValidComp; i++)
    {
      checkTransformSkip[i]=false;
      const ComponentID compID=ComponentID(i);
      pcCoeffCurr[compID]    = m_ppcQTTempCoeff[compID][uiQTTempAccessLayer] + rTu.getCoefficientOffset(compID);
#if ADAPTIVE_QP_SELECTION
      pcArlCoeffCurr[compID] = m_ppcQTTempArlCoeff[compID ][uiQTTempAccessLayer] +  rTu.getCoefficientOffset(compID);
#endif

      if(rTu.ProcessComponentSection(compID))
      {
#if RExt__N0288_SPECIFY_TRANSFORM_SKIP_MAXIMUM_SIZE
        checkTransformSkip[compID] = pcCU->getSlice()->getPPS()->getUseTransformSkip() &&
                                     TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(compID), pcCU->getSlice()->getPPS()->getTransformSkipLog2MaxSize()) &&
                                     (!pcCU->isLosslessCoded(0));
#else
        checkTransformSkip[compID] = pcCU->getSlice()->getPPS()->getUseTransformSkip() && TUCompRectHasAssociatedTransformSkipFlag(rTu.getRect(compID)) && (!pcCU->isLosslessCoded(0));
#endif

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        const Bool splitIntoSubTUs = rTu.getRect(compID).width != rTu.getRect(compID).height;

        TComTURecurse TUIterator(rTu, false, (splitIntoSubTUs ? TComTU::VERTICAL_SPLIT : TComTU::DONT_SPLIT), true, compID);

        const UInt partIdxesPerSubTU = TUIterator.GetAbsPartIdxNumParts(compID);

        do
        {
          const UInt           subTUIndex             = TUIterator.GetSectionNumber();
          const UInt           subTUAbsPartIdx        = TUIterator.GetAbsPartIdxTU(compID);
          const TComRectangle &tuCompRect             = TUIterator.getRect(compID);
          const UInt           subTUBufferOffset      = tuCompRect.width * tuCompRect.height * subTUIndex;

                TCoeff        *currentCoefficients    = pcCoeffCurr[compID] + subTUBufferOffset;
#if ADAPTIVE_QP_SELECTION
                TCoeff        *currentARLCoefficients = pcArlCoeffCurr[compID] + subTUBufferOffset;
#endif
#else
          const TComRectangle &tuCompRect = rTu.getRect(compID);
                TCoeff *currentCoefficients = pcCoeffCurr[compID];
#if ADAPTIVE_QP_SELECTION
                TCoeff *currentARLCoefficients = pcArlCoeffCurr[compID];
#endif
#endif

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          pcCU->setTransformSkipPartRange(0, compID, subTUAbsPartIdx, partIdxesPerSubTU);
#else
          pcCU->setTransformSkipSubParts(0, compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID));
#endif

          if (compID!=COMPONENT_Cr && m_pcEncCfg->getUseRDOQ())
          {
            // assert (rTu.getRect(COMPONENT_Cb).width == rTu.getRect(COMPONENT_Cr).width && rTu.getRect(COMPONENT_Cb).height == rTu.getRect(COMPONENT_Cr).height);
            m_pcEntropyCoder->estimateBit(m_pcTrQuant->m_pcEstBitsSbac, tuCompRect.width, tuCompRect.height, toChannelType(compID));
          }

#if RDOQ_CHROMA_LAMBDA
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_990
          m_pcTrQuant->selectLambda(toChannelType(compID));
#else
          m_pcTrQuant->selectLambda(compID);
#endif
#endif

          const Int chromaOffset = pcCU->getSlice()->getPPS()->getQpOffset(compID) + pcCU->getSlice()->getSliceChromaQpDelta(compID);
          const Int bdOffset     = pcCU->getSlice()->getSPS()->getQpBDOffset(toChannelType(compID));

          QpParam cQP;
          setQPforQuant( cQP, pcCU->getQP( 0 ), toChannelType(compID), bdOffset, chromaOffset, pcCU->getPic()->getChromaFormat(), false );

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          m_pcTrQuant->transformNxN( TUIterator, compID, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ), pcResi->getStride(compID), currentCoefficients,
#else
          m_pcTrQuant->transformNxN( rTu, compID, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ), pcResi->getStride(compID), currentCoefficients,
#endif
#if ADAPTIVE_QP_SELECTION
                                     currentARLCoefficients,
#endif
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
                                     uiAbsSum[compID][subTUIndex], cQP
#else
                                     uiAbsSum[compID], cQP
#endif
                                     );

#if RExt__NRCE2_RESIDUAL_DPCM
          bestInterRdpcmModeUnSplit[compID][subTUIndex] = pcCU->getInterRdpcmMode(compID, subTUAbsPartIdx);
#endif

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          m_pcEntropyCoder->encodeQtCbf( TUIterator, compID, true );
          m_pcEntropyCoder->encodeCoeffNxN( TUIterator, currentCoefficients, compID );

          const UInt newBits=m_pcEntropyCoder->getNumberOfWrittenBits();
          uiSingleBitsComp[compID][subTUIndex]=newBits-uiSingleBitsPrev;
          uiSingleBitsPrev=newBits;
#else
          m_pcEntropyCoder->encodeQtCbf( rTu, compID );
          m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeffCurr[compID], compID );

          const UInt newBits=m_pcEntropyCoder->getNumberOfWrittenBits();
          uiSingleBitsComp[compID]=newBits-uiSingleBitsPrev;
          uiSingleBitsPrev=newBits;
#endif


#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        } //end of sub-TU loop
        while (TUIterator.nextSection(rTu));
#endif
      } // processing section
    } // component loop

    // cost calculations
    for(UInt i=0; i<numValidComp; i++)
    {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
      for (UInt subTUIndex = 0; subTUIndex < 2; subTUIndex++) minCost[i][subTUIndex]=MAX_DOUBLE;
#else
      minCost[i]=MAX_DOUBLE;
#endif
      const ComponentID compID=ComponentID(i);
      if(rTu.ProcessComponentSection(compID))
      {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        const Bool splitIntoSubTUs = rTu.getRect(compID).width != rTu.getRect(compID).height;

        TComTURecurse TUIterator(rTu, false, (splitIntoSubTUs ? TComTU::VERTICAL_SPLIT : TComTU::DONT_SPLIT), true, compID);

        const UInt partIdxesPerSubTU = TUIterator.GetAbsPartIdxNumParts(compID);

        do
        {
          const UInt           subTUIndex             = TUIterator.GetSectionNumber();
          const UInt           subTUAbsPartIdx        = TUIterator.GetAbsPartIdxTU(compID);
          const TComRectangle &tuCompRect             = TUIterator.getRect(compID);
          const UInt           subTUBufferOffset      = tuCompRect.width * tuCompRect.height * subTUIndex;

                TCoeff        *currentCoefficients    = pcCoeffCurr[compID] + subTUBufferOffset;
#else
          const TComRectangle &tuCompRect = rTu.getRect(compID);
                TCoeff *currentCoefficients = pcCoeffCurr[compID];
#endif
          const Int chromaOffset            = pcCU->getSlice()->getPPS()->getQpOffset(compID) + pcCU->getSlice()->getSliceChromaQpDelta(compID);
          const Int  bdOffset               = pcCU->getSlice()->getSPS()->getQpBDOffset(toChannelType(compID));

#if WEIGHTED_CHROMA_DISTORTION
          Distortion uiDistComp = m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], m_pTempPel, tuCompRect.width, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                           pcResi->getStride(compID), tuCompRect.width, tuCompRect.height, compID); // initialized with zero residual destortion
#else
          Distortion uiDistComp = m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], m_pTempPel, tuCompRect.width, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                           pcResi->getStride(compID), tuCompRect.width, tuCompRect.height);
#endif

          if ( puiZeroDist != NULL )
          {
            *puiZeroDist += uiDistComp;
          }

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          if( uiAbsSum[compID][subTUIndex] > 0 ) //if non-zero coefficients are present, a residual needs to be derived for further prediction
#else
          if( uiAbsSum[compID] > 0 ) //if non-zero coefficients are present, a residual needs to be derived for further prediction
#endif
          {
            Pel *pcResiCurrComp = m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 );

            QpParam cQP;
            setQPforQuant( cQP, pcCU->getQP( 0 ), toChannelType(compID), bdOffset, chromaOffset, pcCU->getPic()->getChromaFormat(), false );

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            m_pcTrQuant->invTransformNxN( TUIterator, compID, pcResiCurrComp, m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID), currentCoefficients, cQP DEBUG_STRING_PASS_INTO_OPTIONAL(&(sSingleStringComp[compID]), DEBUG_INTER_CODING_INV_TRAN) );
#else
            m_pcTrQuant->invTransformNxN( rTu, compID, pcResiCurrComp, m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID), pcCoeffCurr[compID], cQP DEBUG_STRING_PASS_INTO_OPTIONAL(&(sSingleStringComp[compID]), DEBUG_INTER_CODING_INV_TRAN) );
#endif


#if WEIGHTED_CHROMA_DISTORTION
            Distortion uiNonzeroDistComp = m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                                    m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                                    pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                                    pcResi->getStride(compID),
                                                                    tuCompRect.width, tuCompRect.height, compID);
#else
            Distortion uiNonzeroDistComp = m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                                    m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                                    pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                                    pcResi->getStride(compID),
                                                                    tuCompRect.width, tuCompRect.height);
#endif

            if (pcCU->isLosslessCoded(0))
            {
              uiDistComp = uiNonzeroDistComp;
            }
            else
            {
              //trial the cost of encoding only zeros
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
              const Double dSingleCostComp = m_pcRdCost->calcRdCost( uiSingleBitsComp[compID][subTUIndex], uiNonzeroDistComp );
#else
              const Double dSingleCostComp = m_pcRdCost->calcRdCost( uiSingleBitsComp[compID], uiNonzeroDistComp );
#endif

              m_pcEntropyCoder->resetBits(); uiSingleBitsPrev=0;

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_986
              m_pcEntropyCoder->encodeQtCbfZero( TUIterator, toChannelType(compID), false);
#else
              m_pcEntropyCoder->encodeQtCbfZero( TUIterator, toChannelType(compID));
#endif
#else
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_986
              m_pcEntropyCoder->encodeQtCbfZero( rTu, toChannelType(compID), false);
#else
              m_pcEntropyCoder->encodeQtCbfZero( rTu, toChannelType(compID));
#endif
#endif
              const UInt uiNullBitsComp   = m_pcEntropyCoder->getNumberOfWrittenBits();
              const Double dNullCostComp  = m_pcRdCost->calcRdCost( uiNullBitsComp, uiDistComp );

              if( dNullCostComp < dSingleCostComp )
              {
                ::memset( currentCoefficients, 0, sizeof( TCoeff ) * tuCompRect.width * tuCompRect.height );
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
                uiAbsSum[compID][subTUIndex] = 0;
                pcCU->setCbfPartRange( 0, compID, subTUAbsPartIdx, partIdxesPerSubTU );
#else
                uiAbsSum[compID] = 0;
                pcCU->setCbfSubParts( 0, compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID) );
#endif
#if defined DEBUG_STRING && DEBUG_INTER_CODING_INV_TRAN
                sSingleStringComp[compID].clear();
#endif
                if( checkTransformSkip[compID] )
                {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
                  minCost[compID][subTUIndex] = dNullCostComp;
#else
                  minCost[compID] = dNullCostComp;
#endif
                }
              }
              else
              {
                uiDistComp = uiNonzeroDistComp;
                if( checkTransformSkip[compID] )
                {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
                  minCost[compID][subTUIndex] = dSingleCostComp;
#else
                  minCost[compID] = dSingleCostComp;
#endif
                }
              }
            }
          }
          else if( checkTransformSkip[compID] )
          {
            m_pcEntropyCoder->resetBits(); uiSingleBitsPrev=0;
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_986
            m_pcEntropyCoder->encodeQtCbfZero( TUIterator, toChannelType(compID), true);
#else
            m_pcEntropyCoder->encodeQtCbfZero( TUIterator, toChannelType(compID) );
#endif
#else
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_986
            m_pcEntropyCoder->encodeQtCbfZero( rTu, toChannelType(compID), true);
#else
            m_pcEntropyCoder->encodeQtCbfZero( rTu, toChannelType(compID) );
#endif
#endif
            const UInt uiNullBitsComp = m_pcEntropyCoder->getNumberOfWrittenBits();
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            minCost[compID][subTUIndex] = m_pcRdCost->calcRdCost( uiNullBitsComp, uiDistComp );
#else
            minCost[compID] = m_pcRdCost->calcRdCost( uiNullBitsComp, uiDistComp );
#endif
          }


#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          uiSingleDistComp[compID][subTUIndex] = uiDistComp;
          if( uiAbsSum[compID][subTUIndex] == 0 )
#else
          uiSingleDistComp[compID] = uiDistComp;
          if( uiAbsSum[compID] == 0 )
#endif
          {
            //set a residual of all zeros
            Pel *pcResiCurrComp =  m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 );
            const UInt uiStride = m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID);
            for( UInt uiY = 0; uiY < tuCompRect.height; ++uiY )
            {
              ::memset( pcResiCurrComp, 0, sizeof(Pel) * tuCompRect.width );
              pcResiCurrComp += uiStride;
            }
          }
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        } //end of sub-TU loop
        while (TUIterator.nextSection(rTu));
#endif
      } // width check
    }
    uiSingleBitsPrev=0;

    for(UInt ci=0; ci<numValidComp; ci++)
    {
      const ComponentID compID = ComponentID(ci);
      const ChannelType chType = toChannelType(compID);

      if( checkTransformSkip[compID] && rTu.ProcessComponentSection(compID))
      {
        if( m_bUseSBACRD && compID!=COMPONENT_Cr)
        {
          m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
        }

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        const Bool splitIntoSubTUs = rTu.getRect(compID).width != rTu.getRect(compID).height;

        TComTURecurse TUIterator(rTu, false, (splitIntoSubTUs ? TComTU::VERTICAL_SPLIT : TComTU::DONT_SPLIT), true, compID);

        const UInt partIdxesPerSubTU = TUIterator.GetAbsPartIdxNumParts(compID);

        do
        {
          const UInt           subTUIndex             = TUIterator.GetSectionNumber();
          const UInt           subTUAbsPartIdx        = TUIterator.GetAbsPartIdxTU(compID);
          const TComRectangle &tuCompRect             = TUIterator.getRect(compID);
          const UInt           subTUBufferOffset      = tuCompRect.width * tuCompRect.height * subTUIndex;

                TCoeff        *currentCoefficients    = pcCoeffCurr[compID] + subTUBufferOffset;
#if ADAPTIVE_QP_SELECTION
                TCoeff        *currentARLCoefficients = pcArlCoeffCurr[compID] + subTUBufferOffset;
#endif
          const UInt           bestCBFComp            = pcCU->getCbf(subTUAbsPartIdx, compID, uiTrMode);
#else
          const TComRectangle &tuCompRect = rTu.getRect(compID);
                TCoeff *currentCoefficients = pcCoeffCurr[compID];
#if ADAPTIVE_QP_SELECTION
                TCoeff *currentARLCoefficients = pcArlCoeffCurr[compID];
#endif
          const UInt bestCBFComp  = pcCU->getCbf(uiAbsPartIdx, compID, uiTrMode);
#endif

          const UInt uiNumSamplesComp     = tuCompRect.width * tuCompRect.height;
          TCoeff uiAbsSumTransformSkipComp;
          Double dSingleCostComp;

          Pel *pcResiCurrComp = m_pcQTTempTComYuv[ uiQTTempAccessLayer ].getAddrPix(compID, tuCompRect.x0, tuCompRect.y0 );
          UInt resiStride     = m_pcQTTempTComYuv[ uiQTTempAccessLayer ].getStride(compID);

          TCoeff bestCoeffComp[MAX_TU_SIZE*MAX_TU_SIZE];
          memcpy( bestCoeffComp, currentCoefficients, sizeof(TCoeff) * uiNumSamplesComp );

#if ADAPTIVE_QP_SELECTION
          TCoeff bestArlCoeffComp[MAX_TU_SIZE*MAX_TU_SIZE];
          memcpy( bestArlCoeffComp, currentARLCoefficients, sizeof(TCoeff) * uiNumSamplesComp );
#endif

          Pel bestResiComp[MAX_TU_SIZE*MAX_TU_SIZE];
          for ( Int i = 0; i < tuCompRect.height; ++i )
          {
            memcpy( &bestResiComp[i*tuCompRect.width], pcResiCurrComp+i*resiStride, sizeof(Pel) * tuCompRect.width );
          }

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          pcCU->setTransformSkipPartRange ( 1, compID, subTUAbsPartIdx, partIdxesPerSubTU);
#else
          pcCU->setTransformSkipSubParts ( 1, compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID));
#endif

          if (m_pcEncCfg->getUseRDOQTS() && compID!=COMPONENT_Cr)
          {
            m_pcEntropyCoder->estimateBit( m_pcTrQuant->m_pcEstBitsSbac, tuCompRect.width, tuCompRect.height, chType );
          }

          const Int chromaOffset = pcCU->getSlice()->getPPS()->getQpOffset(compID) + pcCU->getSlice()->getSliceChromaQpDelta(compID);

          QpParam cQP;
          setQPforQuant( cQP, pcCU->getQP( 0 ), chType, pcCU->getSlice()->getSPS()->getQpBDOffset(chType), chromaOffset, pcCU->getPic()->getChromaFormat(), true );

#if RDOQ_CHROMA_LAMBDA
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_990
          m_pcTrQuant->selectLambda(chType);
#else
          m_pcTrQuant->selectLambda(compID);
#endif
#endif

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          m_pcTrQuant->transformNxN( TUIterator, compID, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ), pcResi->getStride(compID), currentCoefficients,
#else
          m_pcTrQuant->transformNxN( rTu, compID, pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ), pcResi->getStride(compID), currentCoefficients,
#endif
#if ADAPTIVE_QP_SELECTION
                                     currentARLCoefficients,
#endif
                                     uiAbsSumTransformSkipComp, cQP );

          if (compID!=COMPONENT_Cr) { m_pcEntropyCoder->resetBits(); uiSingleBitsPrev=0; }

          Distortion uiNonzeroDistComp = 0;
          DEBUG_STRING_NEW(sSingleStringTS)
          if( uiAbsSumTransformSkipComp != 0 )
          {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            m_pcEntropyCoder->encodeQtCbf( TUIterator, compID, true );
            m_pcEntropyCoder->encodeCoeffNxN( TUIterator, currentCoefficients, compID );
#else
            m_pcEntropyCoder->encodeQtCbf( rTu, compID );
            m_pcEntropyCoder->encodeCoeffNxN( rTu, currentCoefficients, compID );
#endif

            const UInt newBits=m_pcEntropyCoder->getNumberOfWrittenBits();
            const UInt uiTsSingleBitsComp=newBits-uiSingleBitsPrev;
            uiSingleBitsPrev=newBits;

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            m_pcTrQuant->invTransformNxN( TUIterator, compID, pcResiCurrComp, m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID), currentCoefficients, cQP DEBUG_STRING_PASS_INTO_OPTIONAL(&sSingleStringTS, DEBUG_INTER_CODING_INV_TRAN));
#else
            m_pcTrQuant->invTransformNxN( rTu, compID, pcResiCurrComp, m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID), currentCoefficients, cQP DEBUG_STRING_PASS_INTO_OPTIONAL(&sSingleStringTS, DEBUG_INTER_CODING_INV_TRAN));
#endif


#if WEIGHTED_CHROMA_DISTORTION
            uiNonzeroDistComp = m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                          pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          pcResi->getStride(compID),
                                                          tuCompRect.width, tuCompRect.height, compID);
#else
            uiNonzeroDistComp = m_pcRdCost->getDistPart( g_bitDepth[toChannelType(compID)], m_pcQTTempTComYuv[uiQTTempAccessLayer].getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          m_pcQTTempTComYuv[uiQTTempAccessLayer].getStride(compID),
                                                          pcResi->getAddrPix( compID, tuCompRect.x0, tuCompRect.y0 ),
                                                          pcResi->getStride(compID),
                                                          tuCompRect.width, tuCompRect.height);
#endif

            dSingleCostComp = m_pcRdCost->calcRdCost( uiTsSingleBitsComp, uiNonzeroDistComp );
          }

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          if( (uiAbsSumTransformSkipComp == 0) || minCost[compID][subTUIndex] < dSingleCostComp )
          {
            pcCU->setTransformSkipPartRange ( 0, compID, subTUAbsPartIdx, partIdxesPerSubTU );
            pcCU->setCbfPartRange( (bestCBFComp << uiTrMode), compID, subTUAbsPartIdx, partIdxesPerSubTU );
#else
          if( (uiAbsSumTransformSkipComp == 0) || minCost[compID] < dSingleCostComp )
          {
            pcCU->setTransformSkipSubParts ( 0, compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID) );
            pcCU->setCbfSubParts( (bestCBFComp << uiTrMode), compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID) );
#endif

            memcpy( currentCoefficients, bestCoeffComp, sizeof(TCoeff) * uiNumSamplesComp );
#if ADAPTIVE_QP_SELECTION
            memcpy( currentARLCoefficients, bestArlCoeffComp, sizeof(TCoeff) * uiNumSamplesComp );
#endif
            for( Int i = 0; i < tuCompRect.height; ++i )
            {
              memcpy( pcResiCurrComp+i*resiStride, &bestResiComp[i*tuCompRect.width], sizeof(Pel) * tuCompRect.width );
            }
          }
          else
          {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
            uiSingleDistComp[compID][subTUIndex] = uiNonzeroDistComp;
            uiAbsSum[compID][subTUIndex] = uiAbsSumTransformSkipComp;
            uiBestTransformMode[compID][subTUIndex] = 1;
#if RDPCM_INTER_LOSSY
            bestInterRdpcmModeUnSplit[compID][subTUIndex] = pcCU->getInterRdpcmMode(compID, subTUAbsPartIdx);
#endif
#else
            uiSingleDistComp[compID] = uiNonzeroDistComp;
            uiAbsSum[compID] = uiAbsSumTransformSkipComp;
            uiBestTransformMode[compID] = 1;
#if RDPCM_INTER_LOSSY
            bestInterRdpcmModeUnSplit[compID] = pcCU->getInterRdpcmMode(compID, uiAbsPartIdx);
#endif
#endif
#if defined DEBUG_STRING && DEBUG_INTER_CODING_INV_TRAN
            sSingleStringComp[compID].swap(sSingleStringTS);
#endif
          }
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        } //end of sub-TU loop
        while (TUIterator.nextSection(rTu));
#endif
      }
    } // comp loop

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID = ComponentID(ch);
      if (rTu.ProcessComponentSection(compID) && (rTu.getRect(compID).width != rTu.getRect(compID).height))
      {
        offsetSubTUCBFs(rTu, compID); //the CBFs up to now have been defined for two sub-TUs - shift them down a level and replace with the parent level CBF
      }
    }
#endif

    if( m_bUseSBACRD )
    {
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
    }

    m_pcEntropyCoder->resetBits();

    if( uiLog2TrSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
    {
      m_pcEntropyCoder->encodeTransformSubdivFlag( 0, 5 - uiLog2TrSize );
    }

    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const UInt chOrderChange = ((ch + 1) == numValidComp) ? 0 : (ch + 1);
      const ComponentID compID=ComponentID(chOrderChange);
      if( rTu.ProcessComponentSection(compID) )
      {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        m_pcEntropyCoder->encodeQtCbf( rTu, compID, true );
#else
        m_pcEntropyCoder->encodeQtCbf( rTu, compID );
#endif
      }
    }

    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      if (rTu.ProcessComponentSection(compID))
      {
        m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeffCurr[compID], compID );
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        for (UInt subTUIndex = 0; subTUIndex < 2; subTUIndex++) uiSingleDist += uiSingleDistComp[compID][subTUIndex];
#else
        uiSingleDist+=uiSingleDistComp[compID];
#endif
      }
    }

    uiSingleBits = m_pcEntropyCoder->getNumberOfWrittenBits();

    dSingleCost = m_pcRdCost->calcRdCost( uiSingleBits, uiSingleDist );
  } // check full

  // code sub-blocks
  if( bCheckSplit )
  {
    if( m_bUseSBACRD && bCheckFull )
    {
      m_pcRDGoOnSbacCoder->store( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_TEST ] );
      m_pcRDGoOnSbacCoder->load ( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
    }
    Distortion uiSubdivDist = 0;
    UInt       uiSubdivBits = 0;
    Double     dSubdivCost = 0.0;

    //save the non-split CBFs in case we need to restore them later

    UInt bestCBF     [MAX_NUM_COMPONENT];
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
    UInt bestsubTUCBF[MAX_NUM_COMPONENT][2];
#endif
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);

      if (rTu.ProcessComponentSection(compID))
      {
        bestCBF[compID] = pcCU->getCbf(uiAbsPartIdx, compID, uiTrMode);
        
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        const TComRectangle &tuCompRect = rTu.getRect(compID);
        if (tuCompRect.width != tuCompRect.height)
        {
          const UInt partIdxesPerSubTU = rTu.GetAbsPartIdxNumParts(compID) >> 1;

          for (UInt subTU = 0; subTU < 2; subTU++)
            bestsubTUCBF[compID][subTU] = pcCU->getCbf ((uiAbsPartIdx + (subTU * partIdxesPerSubTU)), compID, subTUDepth);
        }
#endif
      }
    }


    TComTURecurse tuRecurseChild(rTu, false);
    const UInt uiQPartNumSubdiv = tuRecurseChild.GetAbsPartIdxNumParts();

    DEBUG_STRING_NEW(sSplitString[MAX_NUM_COMPONENT])

    do
    {
      DEBUG_STRING_NEW(childString)
      xEstimateResidualQT( pcResi, dSubdivCost, uiSubdivBits, uiSubdivDist, bCheckFull ? NULL : puiZeroDist,  tuRecurseChild DEBUG_STRING_PASS_INTO(childString));
#ifdef DEBUG_STRING
      // split the string by component and append to the relevant output (because decoder decodes in channel order, whereas this search searches by TU-order)
      std::size_t lastPos=0;
      const std::size_t endStrng=childString.find(debug_reorder_data_token[MAX_NUM_COMPONENT], lastPos);
      for(UInt ch = 0; ch < numValidComp; ch++)
      {
        if (lastPos!=std::string::npos && childString.find(debug_reorder_data_token[ch], lastPos)==lastPos) lastPos+=strlen(debug_reorder_data_token[ch]); // skip leading string
        std::size_t pos=childString.find(debug_reorder_data_token[ch+1], lastPos);
        if (pos!=std::string::npos && pos>endStrng) lastPos=endStrng;
        sSplitString[ch]+=childString.substr(lastPos, (pos==std::string::npos)? std::string::npos : (pos-lastPos) );
        lastPos=pos;
      }
#endif
    }
    while ( tuRecurseChild.nextSection(rTu) ) ;

    UInt uiCbfAny=0;
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      UInt uiYUVCbf = 0;
      for( UInt ui = 0; ui < 4; ++ui )
      {
        uiYUVCbf |= pcCU->getCbf( uiAbsPartIdx + ui * uiQPartNumSubdiv, ComponentID(ch),  uiTrMode + 1 );
      }
      UChar *pBase=pcCU->getCbf( ComponentID(ch) );
      const UInt flags=uiYUVCbf << uiTrMode;
      for( UInt ui = 0; ui < 4 * uiQPartNumSubdiv; ++ui )
      {
        pBase[uiAbsPartIdx + ui] |= flags;
      }
      uiCbfAny|=uiYUVCbf;
    }

    if( m_bUseSBACRD )
    {
      m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_ROOT ] );
    }
    m_pcEntropyCoder->resetBits();

    // when compID isn't a channel, code Cbfs:
    xEncodeResidualQT( MAX_NUM_COMPONENT, rTu );
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      xEncodeResidualQT( ComponentID(ch), rTu );
    }

    uiSubdivBits = m_pcEntropyCoder->getNumberOfWrittenBits();
    dSubdivCost  = m_pcRdCost->calcRdCost( uiSubdivBits, uiSubdivDist );

    if (!bCheckFull || (uiCbfAny && (dSubdivCost < dSingleCost)))
    {
      rdCost += dSubdivCost;
      ruiBits += uiSubdivBits;
      ruiDist += uiSubdivDist;
#ifdef DEBUG_STRING
      for(UInt ch = 0; ch < numValidComp; ch++)
      {
        DEBUG_STRING_APPEND(sDebug, debug_reorder_data_token[ch])
        DEBUG_STRING_APPEND(sDebug, sSplitString[ch])
      }
#endif
    }
    else
    {
      rdCost  += dSingleCost;
      ruiBits += uiSingleBits;
      ruiDist += uiSingleDist;

      //restore state to unsplit

      pcCU->setTrIdxSubParts( uiTrMode, uiAbsPartIdx, uiDepth );

      for(UInt ch = 0; ch < numValidComp; ch++)
      {
        const ComponentID compID=ComponentID(ch);

        DEBUG_STRING_APPEND(sDebug, debug_reorder_data_token[ch])
        if (rTu.ProcessComponentSection(compID))
        {
          DEBUG_STRING_APPEND(sDebug, sSingleStringComp[compID])

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          const Bool splitIntoSubTUs   = rTu.getRect(compID).width != rTu.getRect(compID).height;
          const UInt numberOfSections  = splitIntoSubTUs ? 2 : 1;
          const UInt partIdxesPerSubTU = rTu.GetAbsPartIdxNumParts(compID) >> (splitIntoSubTUs ? 1 : 0);

          for (UInt subTUIndex = 0; subTUIndex < numberOfSections; subTUIndex++)
          {
            const UInt  uisubTUPartIdx = uiAbsPartIdx + (subTUIndex * partIdxesPerSubTU);
            
            if (splitIntoSubTUs)
            {
              const UChar combinedCBF = (bestsubTUCBF[compID][subTUIndex] << subTUDepth) | (bestCBF[compID] << uiTrMode);
              pcCU->setCbfPartRange(combinedCBF, compID, uisubTUPartIdx, partIdxesPerSubTU);
            }
            else
            {
              pcCU->setCbfPartRange((bestCBF[compID] << uiTrMode), compID, uisubTUPartIdx, partIdxesPerSubTU);
            }

            pcCU->setTransformSkipPartRange(uiBestTransformMode[compID][subTUIndex], compID, uisubTUPartIdx, partIdxesPerSubTU);
#if RExt__NRCE2_RESIDUAL_DPCM
            pcCU->setInterRdpcmModePartRange(bestInterRdpcmModeUnSplit[compID][subTUIndex], compID, uisubTUPartIdx, partIdxesPerSubTU);            
#endif
          }
#else
          pcCU->setTransformSkipSubParts ( uiBestTransformMode[compID], compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID) );
          pcCU->setCbfSubParts( (bestCBF[compID] << uiTrMode), compID, uiAbsPartIdx, rTu.GetTransformDepthTotalAdj(compID) );
#if RExt__NRCE2_RESIDUAL_DPCM
          pcCU->setInterRdpcmModeSubParts (bestInterRdpcmModeUnSplit[compID]            , compID, uiAbsPartIdx,   rTu.GetTransformDepthTotalAdj(compID) );
#endif
#endif
        }
      }

      if( m_bUseSBACRD )
      {
        m_pcRDGoOnSbacCoder->load( m_pppcRDSbacCoder[ uiDepth ][ CI_QT_TRAFO_TEST ] );
      }
    }
  }
  else
  {
    rdCost  += dSingleCost;
    ruiBits += uiSingleBits;
    ruiDist += uiSingleDist;
#ifdef DEBUG_STRING
    for(UInt ch = 0; ch < numValidComp; ch++)
    {
      const ComponentID compID=ComponentID(ch);
      DEBUG_STRING_APPEND(sDebug, debug_reorder_data_token[compID])

      if (rTu.ProcessComponentSection(compID))
      {
        DEBUG_STRING_APPEND(sDebug, sSingleStringComp[compID])
      }
    }
#endif
  }
  DEBUG_STRING_APPEND(sDebug, debug_reorder_data_token[MAX_NUM_COMPONENT])
}



Void TEncSearch::xEncodeResidualQT( const ComponentID compID, TComTU &rTu )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiAbsPartIdx=rTu.GetAbsPartIdxTU();
  const UInt uiCurrTrMode = rTu.GetTransformDepthRel();
  assert( pcCU->getDepth( 0 ) == pcCU->getDepth( uiAbsPartIdx ) );
  const UInt uiTrMode = pcCU->getTransformIdx( uiAbsPartIdx );

  const Bool bSubdiv = uiCurrTrMode != uiTrMode;

  const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();

  if (compID==MAX_NUM_COMPONENT)  // we are not processing a channel, instead we always recurse and code the Cbf's
  {
    if( uiLog2TrSize <= pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() && uiLog2TrSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
    {
      m_pcEntropyCoder->encodeTransformSubdivFlag( bSubdiv, 5 - uiLog2TrSize );
    }

#if RExt__N0256_INTRA_BLOCK_COPY
    assert( !pcCU->isIntra(uiAbsPartIdx) );
#else
    assert( pcCU->getPredictionMode(uiAbsPartIdx) != MODE_INTRA );
#endif

    const Bool bFirstCbfOfCU = uiCurrTrMode == 0;

    for (UInt ch=COMPONENT_Cb; ch<pcCU->getPic()->getNumberValidComponents(); ch++)
    {
      const ComponentID compIdInner=ComponentID(ch);
      if( bFirstCbfOfCU || rTu.ProcessingAllQuadrants(compIdInner) )
      {
        if( bFirstCbfOfCU || pcCU->getCbf( uiAbsPartIdx, compIdInner, uiCurrTrMode - 1 ) )
        {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          m_pcEntropyCoder->encodeQtCbf( rTu, compIdInner, !bSubdiv );
#else
          m_pcEntropyCoder->encodeQtCbf( rTu, compIdInner );
#endif
        }
      }
      else
      {
        assert( pcCU->getCbf( uiAbsPartIdx, compIdInner, uiCurrTrMode ) == pcCU->getCbf( uiAbsPartIdx, compIdInner, uiCurrTrMode - 1 ) );
      }
    }

    if (!bSubdiv)
    {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
      m_pcEntropyCoder->encodeQtCbf( rTu, COMPONENT_Y, true );
#else
      m_pcEntropyCoder->encodeQtCbf( rTu, COMPONENT_Y );
#endif
    }
  }

  if( !bSubdiv )
  {
    if (compID!=MAX_NUM_COMPONENT) // we have already coded the Cbf's, so now we code coefficients
    {
      if (( rTu.ProcessComponentSection(compID)) && (pcCU->getCbf( uiAbsPartIdx, compID,     uiTrMode )) )
      {
        const UInt uiQTTempAccessLayer = pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() - uiLog2TrSize;
        TCoeff *pcCoeffCurr = m_ppcQTTempCoeff[compID][uiQTTempAccessLayer] + rTu.getCoefficientOffset(compID);
        m_pcEntropyCoder->encodeCoeffNxN( rTu, pcCoeffCurr, compID );
      }
    }
  }
  else
  {
    if( compID==MAX_NUM_COMPONENT || pcCU->getCbf( uiAbsPartIdx, compID, uiCurrTrMode ) )
    {
      TComTURecurse tuRecurseChild(rTu, false);
      do
      {
        xEncodeResidualQT( compID, tuRecurseChild );
      } while (tuRecurseChild.nextSection(rTu));
    }
  }
}




Void TEncSearch::xSetResidualQTData( TComYuv* pcResi, Bool bSpatial, TComTU &rTu )
{
  TComDataCU* pcCU=rTu.getCU();
  const UInt uiCurrTrMode=rTu.GetTransformDepthRel();
  const UInt uiAbsPartIdx=rTu.GetAbsPartIdxTU();
  assert( pcCU->getDepth( 0 ) == pcCU->getDepth( uiAbsPartIdx ) );
  const UInt uiTrMode = pcCU->getTransformIdx( uiAbsPartIdx );
  TComSPS *sps=pcCU->getSlice()->getSPS();

  if( uiCurrTrMode == uiTrMode )
  {
    const UInt uiLog2TrSize = rTu.GetLog2LumaTrSize();
    const UInt uiQTTempAccessLayer = sps->getQuadtreeTULog2MaxSize() - uiLog2TrSize;

    if( bSpatial )
    {
      // Data to be copied is in the spatial domain, i.e., inverse-transformed.

      for(UInt i=0; i<pcResi->getNumberValidComponents(); i++)
      {
        const ComponentID compID=ComponentID(i);
        if (rTu.ProcessComponentSection(compID))
        {
          const TComRectangle &rectCompTU(rTu.getRect(compID));
          m_pcQTTempTComYuv[uiQTTempAccessLayer].copyPartToPartComponentMxN    ( compID, pcResi, rectCompTU );
        }
      }
    }
    else
    {
      for (UInt ch=0; ch < getNumberValidComponents(sps->getChromaFormatIdc()); ch++)
      {
        const ComponentID compID   = ComponentID(ch);
        if (rTu.ProcessComponentSection(compID))
        {
          const TComRectangle &rectCompTU(rTu.getRect(compID));
          const UInt numCoeffInBlock    = rectCompTU.width * rectCompTU.height;
          const UInt offset             = rTu.getCoefficientOffset(compID);
          TCoeff* dest                  = pcCU->getCoeff(compID)                        + offset;
          const TCoeff* src             = m_ppcQTTempCoeff[compID][uiQTTempAccessLayer] + offset;
          ::memcpy( dest, src, sizeof(TCoeff)*numCoeffInBlock );

#if ADAPTIVE_QP_SELECTION
          TCoeff* pcArlCoeffSrc            = m_ppcQTTempArlCoeff[compID][uiQTTempAccessLayer] + offset;
          TCoeff* pcArlCoeffDst            = pcCU->getArlCoeff(compID)                        + offset;
          ::memcpy( pcArlCoeffDst, pcArlCoeffSrc, sizeof( TCoeff ) * numCoeffInBlock );
#endif
        }
      }
    }
  }
  else
  {

    TComTURecurse tuRecurseChild(rTu, false);
    do
    {
      xSetResidualQTData( pcResi, bSpatial, tuRecurseChild );
    } while (tuRecurseChild.nextSection(rTu));
  }
}




UInt TEncSearch::xModeBitsIntra( TComDataCU* pcCU, UInt uiMode, UInt uiPartOffset, UInt uiDepth, UInt uiInitTrDepth, const ChannelType chType )
{
  if( m_bUseSBACRD )
  {
    // Reload only contexts required for coding intra mode information
    m_pcRDGoOnSbacCoder->loadIntraDirMode( m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST], chType );
  }

  // Temporarily set the intra dir being tested, and only
  // for absPartIdx, since encodeIntraDirModeLuma/Chroma only use
  // the entry at absPartIdx.

  UChar &rIntraDirVal=pcCU->getIntraDir( chType )[uiPartOffset];
  UChar origVal=rIntraDirVal;
  rIntraDirVal = uiMode;
  //pcCU->setIntraDirSubParts ( chType, uiMode, uiPartOffset, uiDepth + uiInitTrDepth );

  m_pcEntropyCoder->resetBits();
  if (isLuma(chType))
    m_pcEntropyCoder->encodeIntraDirModeLuma ( pcCU, uiPartOffset);
  else
    m_pcEntropyCoder->encodeIntraDirModeChroma ( pcCU, uiPartOffset);

  rIntraDirVal = origVal; // restore

  return m_pcEntropyCoder->getNumberOfWrittenBits();
}




UInt TEncSearch::xUpdateCandList( UInt uiMode, Double uiCost, UInt uiFastCandNum, UInt * CandModeList, Double * CandCostList )
{
  UInt i;
  UInt shift=0;

  while ( shift<uiFastCandNum && uiCost<CandCostList[ uiFastCandNum-1-shift ] ) shift++;

  if( shift!=0 )
  {
    for(i=1; i<shift; i++)
    {
      CandModeList[ uiFastCandNum-i ] = CandModeList[ uiFastCandNum-1-i ];
      CandCostList[ uiFastCandNum-i ] = CandCostList[ uiFastCandNum-1-i ];
    }
    CandModeList[ uiFastCandNum-shift ] = uiMode;
    CandCostList[ uiFastCandNum-shift ] = uiCost;
    return 1;
  }

  return 0;
}





/** add inter-prediction syntax elements for a CU block
 * \param pcCU
 * \param uiQp
 * \param uiTrMode
 * \param ruiBits
 * \param rpcYuvRec
 * \param pcYuvPred
 * \param rpcYuvResi
 * \returns Void
 */
Void  TEncSearch::xAddSymbolBitsInter( TComDataCU* pcCU, UInt uiQp, UInt uiTrMode, UInt& ruiBits, TComYuv*& rpcYuvRec, TComYuv*pcYuvPred, TComYuv*& rpcYuvResi )
{
  if(pcCU->getMergeFlag( 0 ) && pcCU->getPartitionSize( 0 ) == SIZE_2Nx2N && !pcCU->getQtRootCbf( 0 ))
  {
    pcCU->setSkipFlagSubParts( true, 0, pcCU->getDepth(0) );

    m_pcEntropyCoder->resetBits();
    if(pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
    {
      m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, 0, true);
    }
    m_pcEntropyCoder->encodeSkipFlag(pcCU, 0, true);
    m_pcEntropyCoder->encodeMergeIndex(pcCU, 0, true);

    ruiBits += m_pcEntropyCoder->getNumberOfWrittenBits();
  }
  else
  {
    m_pcEntropyCoder->resetBits();
    if(pcCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
    {
      m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, 0, true);
    }
    m_pcEntropyCoder->encodeSkipFlag ( pcCU, 0, true );
#if RExt__N0256_INTRA_BLOCK_COPY
    if (pcCU->getSlice()->getSPS()->getUseIntraBlockCopy())
    {
      m_pcEntropyCoder->encodeIntraBCFlag(pcCU, 0, true);
      if ( pcCU->isIntraBC( 0 ) )
      {
        m_pcEntropyCoder->encodeIntraBC( pcCU, 0 );
      }
    }
    if( !pcCU->isIntraBC(0))
    {
#endif
    m_pcEntropyCoder->encodePredMode( pcCU, 0, true );
    m_pcEntropyCoder->encodePartSize( pcCU, 0, pcCU->getDepth(0), true );
    m_pcEntropyCoder->encodePredInfo( pcCU, 0 );
#if RExt__N0256_INTRA_BLOCK_COPY
    }
#endif
    Bool bDummy = false;
    m_pcEntropyCoder->encodeCoeff   ( pcCU, 0, pcCU->getDepth(0), bDummy );

    ruiBits += m_pcEntropyCoder->getNumberOfWrittenBits();
  }
}





/**
 * \brief Generate half-sample interpolated block
 *
 * \param pattern Reference picture ROI
 * \param biPred    Flag indicating whether block is for biprediction
 */
Void TEncSearch::xExtDIFUpSamplingH( TComPattern* pattern, Bool biPred )
{
  Int width      = pattern->getROIYWidth();
  Int height     = pattern->getROIYHeight();
  Int srcStride  = pattern->getPatternLStride();

  Int intStride = m_filteredBlockTmp[0].getStride(COMPONENT_Y);
  Int dstStride = m_filteredBlock[0][0].getStride(COMPONENT_Y);
  Pel *intPtr;
  Pel *dstPtr;
  Int filterSize = NTAPS_LUMA;
  Int halfFilterSize = (filterSize>>1);
  Pel *srcPtr = pattern->getROIY() - halfFilterSize*srcStride - 1;

  const ChromaFormat chFmt = m_filteredBlock[0][0].getChromaFormat();

  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, m_filteredBlockTmp[0].getAddr(COMPONENT_Y), intStride, width+1, height+filterSize, 0, false, chFmt);
  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, m_filteredBlockTmp[2].getAddr(COMPONENT_Y), intStride, width+1, height+filterSize, 2, false, chFmt);

  intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + halfFilterSize * intStride + 1;
  dstPtr = m_filteredBlock[0][0].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+0, height+0, 0, false, true, chFmt);

  intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride + 1;
  dstPtr = m_filteredBlock[2][0].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+0, height+1, 2, false, true, chFmt);

  intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + halfFilterSize * intStride;
  dstPtr = m_filteredBlock[0][2].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+1, height+0, 0, false, true, chFmt);

  intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[2][2].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width+1, height+1, 2, false, true, chFmt);
}





/**
 * \brief Generate quarter-sample interpolated blocks
 *
 * \param pattern    Reference picture ROI
 * \param halfPelRef Half-pel mv
 * \param biPred     Flag indicating whether block is for biprediction
 */
Void TEncSearch::xExtDIFUpSamplingQ( TComPattern* pattern, TComMv halfPelRef, Bool biPred )
{
  Int width      = pattern->getROIYWidth();
  Int height     = pattern->getROIYHeight();
  Int srcStride  = pattern->getPatternLStride();

  Pel *srcPtr;
  Int intStride = m_filteredBlockTmp[0].getStride(COMPONENT_Y);
  Int dstStride = m_filteredBlock[0][0].getStride(COMPONENT_Y);
  Pel *intPtr;
  Pel *dstPtr;
  Int filterSize = NTAPS_LUMA;

  Int halfFilterSize = (filterSize>>1);

  Int extHeight = (halfPelRef.getVer() == 0) ? height + filterSize : height + filterSize-1;

  const ChromaFormat chFmt = m_filteredBlock[0][0].getChromaFormat();

  // Horizontal filter 1/4
  srcPtr = pattern->getROIY() - halfFilterSize * srcStride - 1;
  intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() > 0)
  {
    srcPtr += srcStride;
  }
  if (halfPelRef.getHor() >= 0)
  {
    srcPtr += 1;
  }
  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, intPtr, intStride, width, extHeight, 1, false, chFmt);

  // Horizontal filter 3/4
  srcPtr = pattern->getROIY() - halfFilterSize*srcStride - 1;
  intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() > 0)
  {
    srcPtr += srcStride;
  }
  if (halfPelRef.getHor() > 0)
  {
    srcPtr += 1;
  }
  m_if.filterHor(COMPONENT_Y, srcPtr, srcStride, intPtr, intStride, width, extHeight, 3, false, chFmt);

  // Generate @ 1,1
  intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[1][1].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() == 0)
  {
    intPtr += intStride;
  }
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt);

  // Generate @ 3,1
  intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[3][1].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt);

  if (halfPelRef.getVer() != 0)
  {
    // Generate @ 2,1
    intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[2][1].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() == 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 2, false, true, chFmt);

    // Generate @ 2,3
    intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[2][3].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() == 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 2, false, true, chFmt);
  }
  else
  {
    // Generate @ 0,1
    intPtr = m_filteredBlockTmp[1].getAddr(COMPONENT_Y) + halfFilterSize * intStride;
    dstPtr = m_filteredBlock[0][1].getAddr(COMPONENT_Y);
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 0, false, true, chFmt);

    // Generate @ 0,3
    intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + halfFilterSize * intStride;
    dstPtr = m_filteredBlock[0][3].getAddr(COMPONENT_Y);
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 0, false, true, chFmt);
  }

  if (halfPelRef.getHor() != 0)
  {
    // Generate @ 1,2
    intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[1][2].getAddr(COMPONENT_Y);
    if (halfPelRef.getHor() > 0)
    {
      intPtr += 1;
    }
    if (halfPelRef.getVer() >= 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt);

    // Generate @ 3,2
    intPtr = m_filteredBlockTmp[2].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
    dstPtr = m_filteredBlock[3][2].getAddr(COMPONENT_Y);
    if (halfPelRef.getHor() > 0)
    {
      intPtr += 1;
    }
    if (halfPelRef.getVer() > 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt);
  }
  else
  {
    // Generate @ 1,0
    intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride + 1;
    dstPtr = m_filteredBlock[1][0].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() >= 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt);

    // Generate @ 3,0
    intPtr = m_filteredBlockTmp[0].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride + 1;
    dstPtr = m_filteredBlock[3][0].getAddr(COMPONENT_Y);
    if (halfPelRef.getVer() > 0)
    {
      intPtr += intStride;
    }
    m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt);
  }

  // Generate @ 1,3
  intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[1][3].getAddr(COMPONENT_Y);
  if (halfPelRef.getVer() == 0)
  {
    intPtr += intStride;
  }
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 1, false, true, chFmt);

  // Generate @ 3,3
  intPtr = m_filteredBlockTmp[3].getAddr(COMPONENT_Y) + (halfFilterSize-1) * intStride;
  dstPtr = m_filteredBlock[3][3].getAddr(COMPONENT_Y);
  m_if.filterVer(COMPONENT_Y, intPtr, intStride, dstPtr, dstStride, width, height, 3, false, true, chFmt);
}





/** set wp tables
 * \param TComDataCU* pcCU
 * \param iRefIdx
 * \param eRefPicListCur
 * \returns Void
 */
Void  TEncSearch::setWpScalingDistParam( TComDataCU* pcCU, Int iRefIdx, RefPicList eRefPicListCur )
{
  if ( iRefIdx<0 )
  {
    m_cDistParam.bApplyWeight = false;
    return;
  }

  TComSlice       *pcSlice  = pcCU->getSlice();
  TComPPS         *pps      = pcCU->getSlice()->getPPS();
  wpScalingParam  *wp0 , *wp1;

  m_cDistParam.bApplyWeight = ( pcSlice->getSliceType()==P_SLICE && pps->getUseWP() ) || ( pcSlice->getSliceType()==B_SLICE && pps->getWPBiPred() ) ;

  if ( !m_cDistParam.bApplyWeight ) return;

  Int iRefIdx0 = ( eRefPicListCur == REF_PIC_LIST_0 ) ? iRefIdx : (-1);
  Int iRefIdx1 = ( eRefPicListCur == REF_PIC_LIST_1 ) ? iRefIdx : (-1);

  getWpScaling( pcCU, iRefIdx0, iRefIdx1, wp0 , wp1 );

  if ( iRefIdx0 < 0 ) wp0 = NULL;
  if ( iRefIdx1 < 0 ) wp1 = NULL;

  m_cDistParam.wpCur  = NULL;

  if ( eRefPicListCur == REF_PIC_LIST_0 )
  {
    m_cDistParam.wpCur = wp0;
  }
  else
  {
    m_cDistParam.wpCur = wp1;
  }
}

//! \}
