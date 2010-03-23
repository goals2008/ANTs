/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkHistogramParzenWindowsListSampleFunction.txx,v $
  Language:  C++
  Date:      $Date: $
  Version:   $Revision: $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkHistogramParzenWindowsListSampleFunction_txx
#define __itkHistogramParzenWindowsListSampleFunction_txx

#include "itkHistogramParzenWindowsListSampleFunction.h"

#include "itkArray.h"
#include "itkBSplineInterpolateImageFunction.h"
#include "itkContinuousIndex.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkDivideByConstantImageFilter.h"
#include "itkStatisticsImageFilter.h"

namespace itk
{
namespace Statistics
{
template <class TListSample, class TOutput, class TCoordRep>
HistogramParzenWindowsListSampleFunction<TListSample, TOutput, TCoordRep>
::HistogramParzenWindowsListSampleFunction()
{
  this->m_NumberOfHistogramBins = 32;
  this->m_Sigma = 1.0;
}

template <class TListSample, class TOutput, class TCoordRep>
HistogramParzenWindowsListSampleFunction<TListSample, TOutput, TCoordRep>
::~HistogramParzenWindowsListSampleFunction()
{
}

template <class TListSample, class TOutput, class TCoordRep>
void
HistogramParzenWindowsListSampleFunction<TListSample, TOutput, TCoordRep>
::SetInputListSample( const InputListSampleType * ptr )
{
  this->m_ListSample = ptr;

  if( !this->m_ListSample )
    {
    itkExceptionMacro( "Attempting to set the input list sample to NULL." );
    }

  if( this->m_ListSample->Size() <= 1 )
    {
    itkWarningMacro( "The input list sample has <= 1 element."
                     << "Function evaluations will be equal to 0." );
    return;
    }

  const unsigned int Dimension =
    this->m_ListSample->GetMeasurementVectorSize();

  /**
   * Find the min/max values to define the histogram domain
   */

  Array<RealType> minValues( Dimension );
  minValues.Fill( NumericTraits<RealType>::max() );
  Array<RealType> maxValues( Dimension );
  maxValues.Fill( NumericTraits<RealType>::NonpositiveMin() );

  typename InputListSampleType::ConstIterator It
    = this->m_ListSample->Begin();
  while( It != this->m_ListSample->End() )
    {
    InputMeasurementVectorType inputMeasurement = It.GetMeasurementVector();
    for( unsigned int d = 0; d < Dimension; d++ )
      {
      if( inputMeasurement[d] < minValues[d] )
        {
        minValues[d] = inputMeasurement[d];
        }
      if( inputMeasurement[d] > maxValues[d] )
        {
        maxValues[d] = inputMeasurement[d];
        }
      }
    ++It;
    }

  this->m_HistogramImages.clear();
  for( unsigned int d = 0; d < Dimension; d++ )
    {
    this->m_HistogramImages.push_back( HistogramImageType::New() );

    typename HistogramImageType::SpacingType spacing;
    spacing[0] = ( maxValues[d] - minValues[d] )
      / static_cast<RealType>( this->m_NumberOfHistogramBins - 1 );

    typename HistogramImageType::PointType origin;
    origin[0] = minValues[d] - 3.0 * ( this->m_Sigma * spacing[0] );

    typename HistogramImageType::SizeType size;
    size[0] = vcl_ceil( ( maxValues[d] + 3.0 * ( this->m_Sigma * spacing[0] )
                          - ( minValues[d] - 3.0 * ( this->m_Sigma * spacing[0] ) ) ) / spacing[0] );

    this->m_HistogramImages[d]->SetOrigin( origin );
    this->m_HistogramImages[d]->SetSpacing( spacing );
    this->m_HistogramImages[d]->SetRegions( size );
    this->m_HistogramImages[d]->Allocate();
    this->m_HistogramImages[d]->FillBuffer( 0 );
    }

  unsigned long count = 0;
  It = this->m_ListSample->Begin();
  while( It != this->m_ListSample->End() )
    {
    InputMeasurementVectorType inputMeasurement = It.GetMeasurementVector();

    RealType newWeight = 1.0;
    if( this->m_Weights.Size() == this->m_ListSample->Size() )
      {
      newWeight = this->m_Weights[count];
      }
    for( unsigned int d = 0; d < Dimension; d++ )
      {
      typename HistogramImageType::PointType point;
      point[0] = inputMeasurement[d];

      ContinuousIndex<double, 1> cidx;
      this->m_HistogramImages[d]->TransformPhysicalPointToContinuousIndex(
        point, cidx );

      typename HistogramImageType::IndexType idx;

      idx[0] = vcl_floor( cidx[0] );
      if( this->m_HistogramImages[d]->GetLargestPossibleRegion().IsInside( idx ) )
        {
        RealType oldWeight = this->m_HistogramImages[d]->GetPixel( idx );
        this->m_HistogramImages[d]->SetPixel( idx,
                                              ( 1.0 - ( cidx[0] - idx[0] ) ) * newWeight + oldWeight );
        }

      idx[0]++;
      if( this->m_HistogramImages[d]->GetLargestPossibleRegion().IsInside( idx ) )
        {
        RealType oldWeight = this->m_HistogramImages[d]->GetPixel( idx );
        this->m_HistogramImages[d]->SetPixel( idx,
                                              ( 1.0 - ( idx[0] - cidx[0] ) ) * newWeight + oldWeight );
        }
      }
    ++count;
    ++It;
    }
  for( unsigned int d = 0; d < Dimension; d++ )
    {
    typedef DiscreteGaussianImageFilter<HistogramImageType, HistogramImageType>
      GaussianFilterType;
    typename GaussianFilterType::Pointer gaussian = GaussianFilterType::New();
    gaussian->SetInput( this->m_HistogramImages[d] );
    gaussian->SetVariance( this->m_Sigma * this->m_Sigma );
    gaussian->SetMaximumError( 0.01 );
    gaussian->SetUseImageSpacing( false );
    gaussian->Update();

    typedef StatisticsImageFilter<HistogramImageType> StatsFilterType;
    typename StatsFilterType::Pointer stats = StatsFilterType::New();
    stats->SetInput( gaussian->GetOutput() );
    stats->Update();

    typedef DivideByConstantImageFilter<HistogramImageType, RealType,
                                        HistogramImageType> DividerType;
    typename DividerType::Pointer divider = DividerType::New();
    divider->SetInput( gaussian->GetOutput() );
    divider->SetConstant( stats->GetSum() );
    divider->Update();
    this->m_HistogramImages[d] = divider->GetOutput();
    }
}

template <class TListSample, class TOutput, class TCoordRep>
TOutput
HistogramParzenWindowsListSampleFunction<TListSample, TOutput, TCoordRep>
::Evaluate( const InputMeasurementVectorType & measurement ) const
{
  if( this->m_ListSample->Size() <= 1 )
    {
    return 0;
    }

  typedef BSplineInterpolateImageFunction<HistogramImageType> InterpolatorType;

  RealType probability = 1.0;
  for( unsigned int d = 0; d < this->m_HistogramImages.size(); d++ )
    {
    typename HistogramImageType::PointType point;
    point[0] = measurement[d];

    typename InterpolatorType::Pointer interpolator = InterpolatorType::New();
    interpolator->SetSplineOrder( 3 );
    interpolator->SetInputImage( this->m_HistogramImages[d] );
    if( interpolator->IsInsideBuffer( point ) )
      {
      probability *= interpolator->Evaluate( point );
      }
    else
      {
      return 0;
      }
    }
  return probability;
}

/**
 * Standard "PrintSelf" method
 */
template <class TListSample, class TOutput, class TCoordRep>
void
HistogramParzenWindowsListSampleFunction<TListSample, TOutput, TCoordRep>
::PrintSelf(
  std::ostream& os,
  Indent indent) const
{
  os << indent << "Sigma: "
     << this->m_Sigma << std::endl;
  os << indent << "Number of histogram bins: "
     << this->m_NumberOfHistogramBins << std::endl;
}
} // end of namespace Statistics
} // end of namespace itk

#endif