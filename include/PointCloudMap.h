/*
 * This file is part of PLVS
 * Copyright (C) 2018-present Luigi Freda <luigifreda at gmail dot com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef POINTCLOUD_MAP_H
#define POINTCLOUD_MAP_H 

#include <math.h> 
#include <mutex>
#include <thread>
#include <chrono>

#include <opencv2/core/core.hpp>

#include "PointDefinitions.h"
#include "PointCloudKeyFrame.h"
#include "PointCloudMapInput.h"

#include <pcl/common/transforms.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/octree/octree_pointcloud.h>
#include <pcl/octree/octree_pointcloud_singlepoint.h>
#include <pcl/octree/octree_impl.h>



namespace PLVS
{


///	\class CameraModelParams
///	\author Luigi Freda
///	\brief Abstract class for managing camera parameters  
///	\note
///	\date
///	\warning
struct CameraModelParams
{
    double fx; 
    double fy; 
    double cx; 
    double cy;
    
    int width;
    int height;
    
    double minDist;
    double maxDist; 
};



///	\class PointCloudMapping
///	\author Luigi Freda
///	\brief Abstract class for merging/managing point clouds 
///	\note
///	\date
///	\warning
template<typename PointT>
class PointCloudMap
{
public:

    static const double kDefaultResolution;
    static const float kMinCosForNormalAssociation;
    static const double kNormThresholdForEqualMatrices; 

public:

    typedef typename pcl::PointCloud<PointT> PointCloudT;

public:

    PointCloudMap(double resolution_in = kDefaultResolution);

    virtual void SetDepthCameraModel(const CameraModelParams& params) {}
    virtual void SetColorCameraModel(const CameraModelParams& params) {}
        
    virtual void InsertData(typename PointCloudMapInput<PointT>::Ptr pData) = 0; 

    virtual int UpdateMap() = 0;
    
    void UpdateMapTimestamp();

    virtual void Clear();
    
    virtual void OnMapChange(){}
    
    virtual void SaveMap(const std::string& filename);
    virtual void SaveTriangleMeshMap(const std::string& filename, bool binary = true);    
    
    virtual bool LoadMap(const std::string& filename);
    
public: /// < getters    

    typename PointCloudT::Ptr GetMap()
    {
        std::unique_lock<std::recursive_timed_mutex> lck(this->pointCloudMutex_);
        return (pPointCloud_? pPointCloud_->makeShared() : 0); // deep copy 
    }
    
    typename PointCloudT::Ptr GetMapWithTimeout(std::chrono::milliseconds& timeout)
    {
        std::unique_lock<std::recursive_timed_mutex> lck(this->pointCloudMutex_,timeout);
        if (lck.owns_lock())
        {
            return (pPointCloud_? pPointCloud_->makeShared() : 0); // deep copy 
        }
        else
        {
            return 0;
        }
    }
    
    virtual void GetMapWithTimeout(typename PointCloudT::Ptr& pCloud, typename PointCloudT::Ptr& pCloudUnstable, 
                                   std::vector<unsigned int>& faces, const std::chrono::milliseconds& timeout, bool copyUnstable = false)
    {
        //std::cout << "PointCloudMap::GetMapWithTimeout() - getting map in " << timeout.count() << " milliseconds" << std::endl;
        std::unique_lock<std::recursive_timed_mutex> lck(this->pointCloudMutex_,timeout);
        if (lck.owns_lock())
        {
            //std::cout << "PointCloudMap::GetMapWithTimeout() - point cloud : " << pPointCloud_ << std::endl;
            pCloud         = pPointCloud_? pPointCloud_->makeShared() : 0; // deep copy
            //std::cout << "PointCloudMap::GetMapWithTimeout() - pPointCloud_ copied " << std::endl;            
            if(copyUnstable)
            {
                //std::cout << "PointCloudMap::GetMapWithTimeout() - copying unstable point cloud " << std::endl;
                pCloudUnstable = pPointCloudUnstable_? pPointCloudUnstable_->makeShared() : 0; // deep copy
            }
            //faces = faces_;
            faces.clear();
            //std::cout << "PointCloudMap::GetMapWithTimeout() - all done " << std::endl;              
        }
        else
        {
            pCloud = 0;
            pCloudUnstable = 0;
            faces.clear();
        }        
    }

    pcl::uint64_t GetMapTimestamp()
    {
        std::unique_lock<std::recursive_timed_mutex> lck(this->pointCloudMutex_);
        return pPointCloud_->header.stamp;
    }

public: /// < setters 

    virtual void SetIntProperty(unsigned int property, int val){}
    virtual void SetBoolProperty(unsigned int property, bool val){}
    virtual void SetFloatProperty(unsigned int property, float val){}
    
    void SetRemoveUnstablePoints(bool val) { bRemoveUnstablePoints_ = val; }
    void SetPerformSegmentation(bool val) { bPerformSegmentation_ = val; }
    void SetPerformCarving(bool val) { bPerformCarving_ = val; }
    void SetDownsampleStep(int val) { nDownsampleStep_ = val; }
    
    void SetResetOnMapChange(bool val) { bResetOnSparseMapChange_ = val; } 
    void SetKfAdjustmentOnMapChange(bool val) { bCloudDeformationOnSparseMapChange_ = val; }     

    void SetMinCosForNormalAssociation(float val) { minCosForNormalAssociation_ = val; }
    
    void ResetPointCloud();
    
    void TransformCameraCloudInWorldFrame(typename PointCloudT::ConstPtr pCloudCamera, const cv::Mat& Twc, typename PointCloudT::Ptr pCloudWorld);  
    void TransformCameraCloudInWorldFrame(const PointCloudT& cloudCamera, const cv::Mat& Twc, PointCloudT& cloudWorld);  
    
protected:

    double resolution_;
    
    bool bResetOnSparseMapChange_;
    bool bCloudDeformationOnSparseMapChange_;

    std::recursive_timed_mutex pointCloudMutex_;
    typename PointCloudT::Ptr pPointCloud_;
    typename PointCloudT::Ptr pPointCloudUnstable_;
    boost::uint64_t lastTimestamp_; // last received data timestamp 
    
    int nDownsampleStep_; 
    
    bool bMapUpdated_;
    bool bRemoveUnstablePoints_; 
    bool bPerformSegmentation_; 
    bool bPerformCarving_; 
    
    float minCosForNormalAssociation_;
    
protected: 

    bool WritePLY(PointCloudT& pCloud, std::string filename, bool isMesh = false, bool binary = true);    
    
    void InvertColors(typename PointCloudT::Ptr pCloud);
    
    void ComputeNormals(typename PointCloudT::Ptr pCloud);
    
};


#if !USE_NORMALS

/// < list here the types you want to use 
template class PointCloudMap<pcl::PointXYZRGBA>;

#else

template class PointCloudMap<pcl::PointXYZRGBNormal>;
template class PointCloudMap<pcl::PointSurfelSegment>;

#endif

} //namespace PLVS



#endif // POINTCLOUD_ENGINE_H
