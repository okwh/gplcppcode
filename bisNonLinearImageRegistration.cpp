/*  License
 
 _This file is Copyright 2018 by the Image Processing and Analysis Group (BioImage Suite Team). Dept. of Radiology & Biomedical Imaging, Yale School of Medicine._ It is released under the terms of the GPL v2.
 
 ----
     
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
   See also  http: www.gnu.org/licenses/gpl.html
   
   If this software is modified please retain this statement and add a notice
   that it had been modified (and by whom).  
 
 Endlicense */

#include "bisNonLinearImageRegistration.h"
#include "bisApproximateDisplacementField.h"
#include "bisImageAlgorithms.h"
#include <sstream>
#include <time.h>

bisNonLinearImageRegistration::bisNonLinearImageRegistration(std::string s) : bisAbstractImageRegistration(s)
{
  std::shared_ptr<bisComboTransformation> tmpc(new bisComboTransformation(this->name+":combo"));
  this->internalTransformation=tmpc;
  this->internalTransformation->identity();
  this->class_name="bisNonLinearImageRegistration";
  this->hasInitialTransformation=0;
  this->append_mode=1;
}

bisNonLinearImageRegistration::~bisNonLinearImageRegistration()
{
  this->lastSmoothness=-1.0;
  this->lastSimilarity=-1.0;
}

void bisNonLinearImageRegistration::setInitialTransformation(std::shared_ptr<bisAbstractTransformation> tr) 
{
  this->initialTransformation=tr;
  this->hasInitialTransformation=1;
}
  

void bisNonLinearImageRegistration::generateFeedback(std::string input)
{
  if (this->enable_feedback)
    std::cout << input << "  (" << this->lastSimilarity << "," << this->lastSmoothness << ")" << std::endl;
}


void bisNonLinearImageRegistration::generateFeedback2(std::string input)
{
  if (this->enable_feedback)
    std::cout << input << std::endl;
}

// Optimizer Stuff
float bisNonLinearImageRegistration::computeValue(std::vector<float>& position)
{
  this->currentGridTransformation->setParameterVector(position);

  bisImageAlgorithms::resliceImage(this->level_target.get(),this->temp_target.get(),this->currentGridTransformation.get(),1,0.0);
  
  short* weight1_ptr=0,*weight2_ptr=0;
  if (this->use_weights>0)
    {
      weight1_ptr=this->level_reference_weight->getImageData();
      if (this->use_weights==2)
	{
	  bisImageAlgorithms::resliceImage(this->level_target_weight.get(),this->temp_target_weight.get(),this->currentGridTransformation.get(),1,0.0);
	  weight2_ptr=this->temp_target_weight->getImageData();
	}
    }

  this->internalHistogram->weightedFillHistogram(this->level_reference->getImageData(),
						 this->temp_target->getImageData(),
						 weight1_ptr,
						 weight2_ptr,
						 use_weights,
						 1.0,
						 1, // reset
						 this->level_dimensions,
						 this->level_bounds);


  
  float mv=(float)this->internalHistogram->computeMetric(this->metric);

  this->lastSimilarity=mv;
  
  if (this->lambda>0.0)
    {
      this->lastSmoothness=this->currentGridTransformation->getTotalBendingEnergy();
      mv+=this->lambda*this->lastSmoothness;
    }
  return mv;
}


float bisNonLinearImageRegistration::computeValueFunctionPiece(bisAbstractTransformation* tr,int bounds[6],int cp)
{

  
  short* weight1_ptr=0,*weight2_ptr=0;
  if (this->use_weights>0)
    weight1_ptr=this->level_reference_weight->getImageData();
  if (this->use_weights==2)
    weight2_ptr=this->temp_target_weight->getImageData();

  int level_dimensions[3]; level_target->getImageDimensions(level_dimensions);

  // Backup
  this->internalHistogram->backup();
  // Remove Part
    this->internalHistogram->weightedFillHistogram(this->level_reference->getImageData(),
						   this->temp_target->getImageData(),
						   weight1_ptr,
						   weight2_ptr,
						   use_weights,
						   -1.0, // factor
						   0, // reset
						   level_dimensions,
						   bounds);

  // Reslice into Part
  bisImageAlgorithms::resliceImageWithBounds(this->level_target.get(),this->part_temp_target.get(),tr,bounds,1,0.0);
  this->internalHistogram->weightedFillHistogram(this->level_reference->getImageData(),
						 this->part_temp_target->getImageData(),
						 weight1_ptr,
						 weight2_ptr,
						 use_weights,
						 1.0, // factor
						 0, // reset
						 level_dimensions,
						 bounds);
  
  // Compute Metric
  float mv=(float)this->internalHistogram->computeMetric(this->metric);

  // Restore histogram
  this->internalHistogram->restore();
  
  if (this->lambda>0.0)
    mv+=this->lambda*this->currentGridTransformation->getBendingEnergyAtControlPoint(cp);
  
  

  return mv;
}


float bisNonLinearImageRegistration::computeGradient(std::vector<float>& params,std::vector<float>& grad)
{
  int dim_ref[3]; level_reference->getImageDimensions(dim_ref);
  float spa_ref[3]; level_reference->getImageSpacing(spa_ref);
  float window_size=1.0;
  
  return this->currentGridTransformation->computeGradientForOptimization(params,grad,
                                                                         this->current_step_size,
                                                                         dim_ref,spa_ref,window_size,
                                                                         this);

}

  
int bisNonLinearImageRegistration::checkInputParameters(bisJSONParameterList* plist)
{

  bisAbstractImageRegistration::checkInputParameters(plist);
  this->internalParameters->setFloatValue("cps",bisUtil::frange(plist->getFloatValue("cps",20.0f),0.1f,50.0f));
  this->internalParameters->setFloatValue("cpsrate",bisUtil::frange(plist->getFloatValue("cpsrate",2.0f),1.0f,2.0f));
  this->internalParameters->setFloatValue("lambda",bisUtil::frange(plist->getFloatValue("lambda",0.0f),0.0f,1.0f));
  this->internalParameters->setFloatValue("windowsize",bisUtil::frange(plist->getFloatValue("windowsize",1.0f),1.0f,2.0f));
  this->internalParameters->setBooleanValue("appendmode",plist->getBooleanValue("appendmode",1));

  if (this->enable_feedback)
    this->internalParameters->print("Fixed Parameters prior to running Non Linear","+ + + ");

  this->lambda=this->internalParameters->getFloatValue("lambda",0.0f);
  this->windowsize=  this->internalParameters->getFloatValue("windowsize",1.0f);
  
  return 1;
}

// ------------------------------------------------------------------------------------------
std::unique_ptr<bisSimpleImage<float> > bisNonLinearImageRegistration::computeDisplacementField(bisAbstractTransformation* old)
{

  int dim_ref[3]; level_reference->getImageDimensions(dim_ref);
  float spa_ref[3]; level_reference->getImageSpacing(spa_ref);
  int newdim[3]; float newspa[3];
  for (int ia=0;ia<=2;ia++) {
    newspa[ia]=this->current_cps[ia]/3.0;
    newdim[ia]=int( ((dim_ref[ia]+1)*spa_ref[ia])/newspa[ia]+0.5)-1;
  }

  this->generateFeedback2("++  ");
  std::cout << "+ + Computing displacement field to fit. Dim=" <<newdim[0] << "," << newdim[1] << "," << newdim[2] <<
    ", spa=" << newspa[0] << "," << newspa[1] << "," << newspa[2] << std::endl;
  std::unique_ptr< bisSimpleImage<float> > disp_field(old->computeDisplacementField(newdim,newspa));
  return std::move(disp_field);
}

void bisNonLinearImageRegistration::approximateDisplacementField(bisSimpleImage<float>* dispfield,bisGridTransformation* newgrd,int fast)

{
  // Compute the displacement field from old
  float spa_ref[3]; level_reference->getImageSpacing(spa_ref);
  
  std::unique_ptr<bisJSONParameterList> params(new bisJSONParameterList());
  params->setFloatValue("lambda",0.1);
  params->setFloatValue("tolerance",spa_ref[0]*0.02);
  params->setIntValue("inverse",0);
  if (fast)
    {
      params->setIntValue("levels",1);
      params->setFloatValue("resolution",1.0);
      params->setIntValue("steps",2);
      params->setIntValue("iterations",5);
      params->setFloatValue("stepsize",0.125);
    }
  else
    {
      params->setIntValue("levels",2);
      params->setFloatValue("resolution",1.0);
      params->setIntValue("steps",3);
      params->setIntValue("iterations",10);
      params->setFloatValue("stepsize",0.125);
    }

  std::unique_ptr<bisApproximateDisplacementField> reg(new bisApproximateDisplacementField("approx"));
  reg->run(dispfield,newgrd,params.get());
}

// ------------------------------------------------------------------------------------------

void bisNonLinearImageRegistration::initializeLevelAndGrid(int lv,int numlevels)
{

  std::cout << "+ + I n i t i a l i z i n g  L e v e l " << lv << std::endl;

  if (lv==numlevels && this->hasInitialTransformation)
    {
      // If this is the lowest resolution we need to figure out how to handle internal
      
      int islinear=this->initialTransformation->isLinear();
      
      std::cout << "+ + \t we have an initial transformation, linear=" << islinear << std::endl;
      
      if (islinear==1) {
        // If it is linear, we are happy no big deal just copy its
        std::cout << "+ + \t this is a pure linear transformation " << std::endl;
        bisMatrixTransformation* linear=(bisMatrixTransformation*)(this->initialTransformation.get());
        bisUtil::mat44 m; linear->getMatrix(m);
        linear->printSelf();
        this->internalTransformation->setInitialTransformation(m);
      } else if (islinear == -1) {
        std::cout << "+ + \t this is a combo transformation " << std::endl;
        bisComboTransformation* in=(bisComboTransformation*)(this->initialTransformation.get());
        bisUtil::mat44 m; in->getInitialTransformation(m);
        bisUtil::printMatrix(m,"linear component");
        this->internalTransformation->setInitialTransformation(m);
      }
    }

  if (this->append_mode)
    {
      bisAbstractImageRegistration::initializeLevel(lv,this->internalTransformation.get());
    }
  else
    {
      bisUtil::mat44 m;
      this->internalTransformation->getInitialTransformation(m);
      std::unique_ptr<bisMatrixTransformation> temp_linear(new bisMatrixTransformation("temp_linear"));
      temp_linear->setMatrix(m);
      bisAbstractImageRegistration::initializeLevel(lv,temp_linear.get());
    }

  
  
  std::unique_ptr<bisSimpleImage<short> > tmp(new bisSimpleImage<short>(this->name+":part_temp_target_image"));
  this->part_temp_target=std::move(tmp);
  this->part_temp_target->copyStructure(this->level_reference.get());



  float cps=this->internalParameters->getFloatValue("cps",20.0);
  float rate=this->internalParameters->getFloatValue("cpsrate",2.0);
  cps=cps*powf(rate,lv-1.0f);

  int dim_ref[3]; level_reference->getImageDimensions(dim_ref);
  float spa_ref[3]; level_reference->getImageSpacing(spa_ref);

  float grid_ori[3] = { 0,0,0};


  
  for (int ia=0;ia<=2;ia++)
    {
      float imagesize=(dim_ref[ia]-1)*spa_ref[ia]+1;
      int numcp=int(imagesize/cps+0.5);
      if (numcp<4)
	numcp=4;
      this->current_dim[ia]=numcp;
      this->current_cps[ia]=imagesize/(this->current_dim[ia]-1.05f);
      float outsz=(numcp-1)*this->current_cps[ia];
      float offset=outsz-imagesize;
      grid_ori[ia]=-0.5f*offset;

    }

  std::stringstream strss;
  strss << this->name << "_grid_" << lv;

  
  // Do we need to approximate the grid
  if (lv !=numlevels && append_mode==0)
    {
      // Fit the previous grid
      std::cout << "+ + ================================================" << std::endl;
      std::cout << "+ + Fitting previous grid" << std::endl;


      std::unique_ptr<bisSimpleImage<float> > dispfield(this->computeDisplacementField(this->currentGridTransformation.get()));
      this->currentGridTransformation->initializeGrid(this->current_dim,this->current_cps,grid_ori,1);
      this->approximateDisplacementField(dispfield.get(),this->currentGridTransformation.get(),1);
      std::cout << "+ + ================================================" << std::endl;
    }
  else
    {
      // First create a new grid
      std::shared_ptr<bisGridTransformation> tmp_g(new bisGridTransformation(strss.str()));
      this->currentGridTransformation=std::move(tmp_g);
      this->currentGridTransformation->initializeGrid(this->current_dim,this->current_cps,grid_ori,1);
      
      
      // Then check if need to fit it
      
      if (lv==numlevels && this->hasInitialTransformation)
        {
          int islinear=this->initialTransformation->isLinear();
          if (islinear == -1)
            {
              // We have a combo
              bisComboTransformation* initial=(bisComboTransformation*)(this->initialTransformation.get());
              
              // store this for now
              bisUtil::mat44 oldlinear; initial->getInitialTransformation(oldlinear);
              
              std::unique_ptr<bisMatrixTransformation> ident(new bisMatrixTransformation);
              ident->identity();
              initial->setInitialTransformation(ident.get());
              std::unique_ptr<bisSimpleImage<float> > dispfield(this->computeDisplacementField(initial));
              this->approximateDisplacementField(dispfield.get(),this->currentGridTransformation.get(),0);
              initial->setInitialTransformation(oldlinear);
            }
        }

      // Add the grid to the back of the internalTransformation Combo
      this->internalTransformation->addTransformation(this->currentGridTransformation);
    }
}


// Set Parameters and Run
void bisNonLinearImageRegistration::run(bisJSONParameterList* plist)
{
  this->checkInputParameters(plist);

  this->generateFeedback2("+ + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +");


  std::stringstream strss;
  strss.precision(5);

  int numlevels=  this->internalParameters->getIntValue("levels");
  int numsteps=   this->internalParameters->getIntValue("steps");
  float stepsize=   this->internalParameters->getFloatValue("stepsize");
  int optimization=this->internalParameters->getIntValue("optimization");
  int iterations=this->internalParameters->getIntValue("iterations");
  float tolerance=this->internalParameters->getFloatValue("tolerance",0.001f);
  this->append_mode=this->internalParameters->getBooleanValue("appendmode",1);
  
  // Also cps, cpsrate, windowsize, lambda ...

  if (this->enable_feedback)
    {
      std::cout << "+ +  Retrieved parameters: nlevels=" << numlevels << " numsteps=" << numsteps << " stepsize=" << stepsize << std::endl;
      std::cout << "+ +      optimization=" << optimization << " iterations=" << iterations << " tolerance=" << tolerance << std::endl;
      std::cout << "+ +      similarity metric=" << metric << ", append_mode=" << this->append_mode << std::endl; // TOADD cps, cpsrate, windowsize
    }

  time_t timer1,timer2;
  time(&timer1);
  for (int level=numlevels;level>=1;level=level-1)
    {
      strss.clear();
      std::stringstream strss2;
      strss2 << "+ +  Beginning to compute  n o n l i n e a r  registration at level=" << level << ", numsteps=" << numsteps << ", tolerance=" << tolerance;
      this->generateFeedback2(strss2.str());
      this->generateFeedback2("+ + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +");
      this->initializeLevelAndGrid(level,numlevels);

      this->generateFeedback2("+ + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +");
      float spa[3]; this->level_reference->getImageSpacing(spa);
      int numdof=this->currentGridTransformation->getNumberOfDOF();
      this->current_step_size=stepsize*powf(2.0f,float(numsteps-1))*spa[0];
      this->generateFeedback2("+ + ");
      std::stringstream strss3;
      strss3 << "+ +  \t\t Beginning level=" << level << " resolution=" << spa[0] << " numdof=" << numdof << " current_step=" << this->current_step_size;
      std::stringstream strss4;
      strss4 << "+ +  \t\t transformation: dim=(" << this->current_dim[0] << "*" << this->current_dim[1] << "*" << this->current_dim[2] << "), ";
      strss4 << "spa=(" << this->current_cps[0] << "," << this->current_cps[1] << "," << this->current_cps[2] << ") ";
      this->generateFeedback2(strss3.str());
      this->generateFeedback2(strss4.str());
      this->generateFeedback2("+ + ");
      // Set stepsize


      std::unique_ptr<bisOptimizer> optimizer(new bisOptimizer(this));

      std::vector<float> position(numdof);
  
      // Get current state ...
      this->currentGridTransformation->getParameterVector(position);
      this->totaltime=0.0;


      for (int step=numsteps;step>=1;step=step-1)
	{
	  if (this->enable_feedback)
	    std::cout << "+ +  In step = " << step << ". Iterations = " << iterations << ", optimization=" << optimization <<", current=" << this->current_step_size << "." << std::endl;
	  if (optimization==1)
	    optimizer->computeGradientDescent(position,iterations,tolerance);
	  else
	    optimizer->computeConjugateGradient(position,iterations,tolerance);
	  
	  this->current_step_size=this->current_step_size/2.0f;
	}
      this->generateFeedback2("+ + ");
      this->generateFeedback2("+ + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +");

    }
  time(&timer2);
  std::stringstream strss_final;
  this->totaltime=difftime(timer2,timer1);
  strss_final << "+ +  Stats : total_time " <<  this->totaltime;
  this->generateFeedback2(strss_final.str());
  this->generateFeedback2("+ + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +");

}




