#ifndef MCS_SLIDING_WINDOW_H
#define MCS_SLIDING_WINDOW_H
#include <deque>
#include <memory>
#include <mutex>
#include "Frame.h"


#include <glog/logging.h>

#include <gtsam/base/Matrix.h>
#include <gtsam/base/Vector.h>

#include <gtsam/inference/Key.h>
#include <gtsam/slam/PriorFactor.h>
#include <gtsam/slam/BetweenFactor.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/GaussNewtonOptimizer.h>


#include <gtsam/inference/Ordering.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>

#include <gtsam/linear/Preconditioner.h>
#include <gtsam/linear/PCGSolver.h>

#include <gtsam/nonlinear/DoglegOptimizer.h>

#include <gtsam/nonlinear/Marginals.h>
#include <gtsam/nonlinear/Values.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/nonlinear/NonlinearISAM.h>
#include <gtsam/nonlinear/NonlinearEquality.h>

#include <gtsam/slam/dataset.h>
#include <gtsam/nonlinear/ISAM2.h>
#include <gtsam/nonlinear/ISAM2Params.h>
#include <gtsam/slam/SmartFactorParams.h> //like chi2 outlier select.
#include <gtsam/slam/GeneralSFMFactor.h>

#include <gtsam/geometry/Cal3_S2Stereo.h>
#include <gtsam/slam/StereoFactor.h>
#include <gtsam_unstable/slam/SmartStereoProjectionPoseFactor.h>

#include <gtsam_unstable/nonlinear/ConcurrentBatchFilter.h>
#include <gtsam_unstable/nonlinear/ConcurrentBatchSmoother.h>
#include <gtsam_unstable/nonlinear/BatchFixedLagSmoother.h>


#include <gtsam/slam/ProjectionFactor.h>
#include <gtsam/slam/PoseTranslationPrior.h>
#include <gtsam/base/numericalDerivative.h>
#include <gtsam/base/serializationTestHelpers.h>
//#include <gtsam/geometry/Rot3.h>
//#include <gtsam/geometry/Point3.h>
//#include <gtsam/geometry/Pose3.h>


#include <opencv2/core/persistence.hpp>
#include <opencv2/core/eigen.hpp>
#include "opencv2/features2d.hpp"
#include "opencv2/xfeatures2d.hpp"
#include "opencv2/video/tracking.hpp"
#include "opencv2/calib3d.hpp"


#include "FrameManager.h"

#include <thread>
#include <opencv2/core/eigen.hpp>
#include "IMU_Preint_GTSAM.h"
#include "FrameWiseGeometry.h"
#include <iostream>
#include "utils/Timer.h"
#include "Visualization.h"
#include "ReprojectionInfoDatabase.h"

using namespace gtsam;
using namespace std;
using namespace cv;

namespace mcs
{

//滑动窗口类
class SlidingWindow
{
private:
    //deque<shared_ptr<Frame> > ordinaryFrameQueue;
    //deque<shared_ptr<Frame> > KF_queue;
    //vector<weak_ptr<LandmarkProperties> vLandmarks;//shared_ptr保存在对应的kf里.

    //扩展kf的内容.这里不需要外部Frame结构知道这种扩展.
    //map<int,LandmarkProperties> KF_landmark_extension_map;
    ReprojectionInfoDatabase reproj_db;
    //const int max_kf_count = 5;
    const int max_kf_count = 3;//debug.
    int cam_count;
    //vector<int> KF_id_queue;
    deque<int> KF_id_queue;
    double getAverageDisp(const ReprojectionRecordT& reproj_info,int ref_kf_id);
    StatisticOfTrackingState current_track_state;


public:
    SlidingWindow(int cam_count)
    {
        this->cam_count = cam_count;
    }
    shared_ptr<Frame> getFrameByID(int frame_id)
    {
        return this->reproj_db.frameTable.query(frame_id);
    }
    inline shared_ptr<Frame> getLastKF()
    {
        return getFrameByID(KF_id_queue.back());
    }
    deque<int> getKFidQueue()
    {
        return this->KF_id_queue;
//        vector<int> retval;
//        for(int i = 0;i<this->KF_id_queue.size();i++)
//        {
//            retval.push_back(KF_id_queue.at(i));
//        }
//        return retval;
    }
    shared_ptr<Values> optimizeFactorGraph(shared_ptr<NonlinearFactorGraph> pGraph,shared_ptr<Values> pInitialEstimate,int MaxIter = 15)
    {
        ScopeTimer timer_optimizer("optimizeFactorGraph()");
        //初始化一个Levenburg-Marquardt优化器,解优化图.返回估计值.
        LevenbergMarquardtParams params;
        params.setVerbosity("ERROR");
        params.linearSolverType = NonlinearOptimizerParams::MULTIFRONTAL_QR;
        params.setMaxIterations(MaxIter);//如果不设置,有时候甚至能优化90多次.大部分时间都消耗在这里.其他在30ms左右,这里300ms.
        params.verbosityLM = LevenbergMarquardtParams::LAMBDA;
        LevenbergMarquardtOptimizer optimizer(*pGraph, *pInitialEstimate, params);


//        DoglegParams params;
//        params.setVerbosity("ERROR");
//        params.linearSolverType = NonlinearOptimizerParams::MULTIFRONTAL_QR;
//        params.setMaxIterations(MaxIter);//如果不设置,有时候甚至能优化90多次.大部分时间都消耗在这里.其他在30ms左右,这里300ms.
//        DoglegOptimizer optimizer(*pGraph,*pInitialEstimate,params);//为什么这个用不了?

        //DEBUG ONLY
//        {
//            cout<<"Before optimization:"<<endl;
//            pInitialEstimate->print();
//        }


        Values *pResult = new Values();
        cout<<"Loss info of optimizer:"<<endl;//详细的信息.
        cout<<"before optimizing:"<<endl;
        pGraph->printErrors(*pInitialEstimate);
        *pResult = optimizer.optimize();//TODO:换成optimizer.optimizeSafely(),避免出现异常.
        LOG(WARNING)<<"Loss after "<<MaxIter<<" times optimiziation:"<<pGraph->error(*pResult)<<endl;
        cout<<"optimized result:"<<endl;
        pResult->print();
        //修改对应Frame和Landmark的值.
        for(auto key_value = pResult->begin(); key_value != pResult->end(); ++key_value)
        {
            auto key = key_value->key;
            Symbol asSymbol(key);
            //Symbol之间的关系:

            //Xi-j 相机的位姿
            //Fi Frame i 的位姿
            //Xi-0 Xi-cam_count -1与Fi有位置约束.

            //Lj Landmark j的位置.
            if(asSymbol.chr() == 'F')
            {//对所有的Frame:
                Pose3 value = key_value->value.cast<Pose3>();
                int x_index = asSymbol.index();
                //查找对应的item X - x_index;
                auto pFrame = this->getFrameByID(x_index);
                //TODO:这里一个Frame有6个姿态.怎么融合?取哪个?
                Rot3 rot;Point3 trans;
                rot = value.rotation();
                trans = value.translation();
                pFrame->setRotationAndTranslation(rot.matrix(),trans.vector());
            }
            else if(asSymbol.chr() == 'L')
            {
                Point3 value = key_value->value.cast<Point3>();
                int landmark_id = asSymbol.index();
                auto pLandmark = this->reproj_db.landmarkTable.query(landmark_id);
                pLandmark->setEstimatedPosition(value);
            }
        }
//        cout<<"loss after optimization variables:(Marginals):"<<endl;
//        Marginals marginals(*pGraph, *pResult);
//        for(auto& key_:pResult->keys())
//        {
//            Symbol asSymbol(key_);
//            cout<<asSymbol.chr()<<asSymbol.index()<<"covariance is:\n" << marginals.marginalCovariance(key_) << endl;
//            LOG(INFO)<<asSymbol.chr()<<asSymbol.index()<<"covariance is:\n" << marginals.marginalCovariance(key_) << endl;
//        }
//        cout<<"errors of factors:"<<endl;
        pGraph->printErrors(*pResult);
        //marginals.print();
        //cout << "x1 covariance:\n" << marginals.marginalCovariance(1) << endl;
        //cout << "x2 covariance:\n" << marginals.marginalCovariance(2) << endl;
        //cout << "x3 covariance:\n" << marginals.marginalCovariance(3) << endl;
        return shared_ptr<Values>(pResult);
    }
    void trackAndKeepReprojectionDBForFrame(shared_ptr<Frame> pFrame,StatisticOfTrackingState& trackState_output)//这是普通帧和关键帧公用的.
    {
        ScopeTimer timer__("trackAndKeepReprojectionDBForFrame()");
        this->reproj_db.frameTable.insertFrame(pFrame);//加入frame_id;
        if(pFrame->frame_id == 0)
        {
            return;//初始帧不追踪.
        }
        int kf_vec_len = this->getInWindKFidVec().size();
        LOG(INFO)<<"In trackAndKeepReprojectionDBForFrame():tracking wind size:"<<kf_vec_len<<endl;
        for(const int& ref_kf_id:this->getInWindKFidVec())//对当前帧,追踪仍在窗口内的关键帧.
        {
            //第一步 跟踪特征点,创建关联关系.
            auto pRefKF = getFrameByID(ref_kf_id);
            const int cam_count = pFrame->get_cam_num();
            vector<vector<Point2f> > vvLeftp2d;//,vvRightp2d;
            vector<vector<float> > vv_disps;//移除vvRightp2d.
            vector<vector<char> > vv_track_type;//跟踪成功与否,跟踪的结果.

            vvLeftp2d.resize(cam_count);vv_disps.resize(cam_count);vv_track_type.resize(cam_count);//vvRightp2d.resize(cam_count);
            vector<map<int,int> > v_p2d_to_kf_p3d_index,v_p2d_to_kf_p2d_index;
            v_p2d_to_kf_p3d_index.resize(cam_count);v_p2d_to_kf_p2d_index.resize(cam_count);
            vector<vector<Point2f> >  v_originalKFP2d_relative;//对应的关键帧p2d.
            v_originalKFP2d_relative.resize(cam_count);
            vector<char> v_track_success;
            v_track_success.resize(cam_count);
            vector<int> v_track_success_count;
            v_track_success_count.resize(cam_count);
            for(int i = 0;i<cam_count;i++)//这里可以多线程.暂时不用.
            {
                ScopeTimer t_cvdopyrlk("track doTrackLastKF_all2dpts");//DEBUG ONLY!
                //doTrackLastKF(pFrame,getLastKF(),i,vvLeftp2d.at(i),vvRightp2d.at(i),v_p2d_to_kf_p3d_index.at(i),v_originalKFP2d_relative.at(i),&v_track_success.at(i),&v_track_success_count.at(i));
                //doTrackLastKF(...);//跟踪窗口里的所有kf.处理所有情况(mono2mono,mono2stereo,stereo2mono,stereo2stereo.)
                //TODO:start a thread rather than invoke doTrackLastKF_all2dpts() directly.
                doTrackLaskKF_all2dpts(pFrame,getFrameByID(ref_kf_id),i,
                                       //this->getFrameByID(ref_kf_id)->p2d_vv.at(i),
                                       vvLeftp2d.at(i),
                                       vv_disps.at(i),vv_track_type.at(i),v_p2d_to_kf_p2d_index.at(i),v_p2d_to_kf_p3d_index.at(i),
                                       &(v_track_success.at(i)),&(v_track_success_count.at(i))
                                       );
            }
            //threads.join();//合并结果集.

            pFrame->reproj_map[ref_kf_id] = ReprojectionRecordT();
            pFrame->reproj_map.at(ref_kf_id).resize(cam_count);
            //改Frame之间引用关系.
            //是不是上一帧?
            pFrame->setReferringID(ref_kf_id);//最后一个是上一个.

            //第二步 建立地图点跟踪关系 根据结果集维护数据库.
            {
                ScopeTimer t_trackDB("keep tracking db infomation.");
                StatisticOfTrackingState& trackingState = trackState_output;
                trackingState.tracking_avail_failed_count_of_each_cam.resize(pFrame->get_cam_num());

                for(int i = 0 ;i<cam_count;i++)//这是一个不可重入过程.数据库本身访问暂时没有锁.暂时估计应该不需要.
                {
                    float camfx,camfy,camcx,camcy;
                    pFrame->cam_info_stereo_vec.at(i).getCamMatFxFyCxCy(camfx,camfy,camcx,camcy);
                    if(v_track_success.at(i))
                    {
                        //反复查表,改数据库.
                        //pFrame->reproj_map.at(ref_kf_id).at(i).push_back();
                        //        this->reproj_db.
                        //step<1>.改frame和相应ReprojectionInfo结构
                        for(int current_frame_p2d_index = 0;current_frame_p2d_index<vvLeftp2d.at(i).size();current_frame_p2d_index++)
                        {
                            SingleProjectionT proj_;
                            assert (vv_disps.at(i).size() == vvLeftp2d.at(i).size());
                            proj_.current_frame_p2d = vvLeftp2d.at(i).at(current_frame_p2d_index);
                            proj_.disp = vv_disps.at(i).at(current_frame_p2d_index);
                            proj_.ref_p2d_id = v_p2d_to_kf_p2d_index.at(i).at(current_frame_p2d_index);
                            proj_.tracking_state = vv_track_type.at(i).at(current_frame_p2d_index);
                            pFrame->reproj_map.at(ref_kf_id).at(i).push_back(proj_);//.at(current_frame_p2d_index) = proj_;
                            //查询是否存在对应的 landmark结构.如果不存在,创建一个.
                            if(proj_.tracking_state == TRACK_STEREO2STEREO|| proj_.tracking_state == TRACK_STEREO2MONO||proj_.tracking_state == TRACK_MONO2STEREO
                                    //||proj_.tracking_state == TRACK_MONO2MONO // 暂时不管.

                                    )//如果这个点可直接三角化.
                            {
                                trackingState.tracking_avail_failed_count_of_each_cam.at(i).avail+=1;

                                //if(pRefKF->kf_p2d_to_landmark_id.at(i).count(proj_.ref_p2d_id) == 0)
                                if(pRefKF->getLandmarkIDByCamIndexAndp2dIndex(i,proj_.ref_p2d_id)<0)
                                {//创建landmark
                                    shared_ptr<LandmarkProperties> pLandmark(new LandmarkProperties());
                                    pLandmark->cam_id = i;
                                    pLandmark->created_by_kf_id = ref_kf_id;
                                    pLandmark->landmark_reference_time = 1; //创建那一次不算.
                                    pLandmark->relative_kf_p2d_id = proj_.ref_p2d_id;
                                    this->reproj_db.landmarkTable.insertLandmark(pLandmark);//维护索引.从此以后它就有id了.
                                    pRefKF->insertLandmarkIDByCamIndexAndp2dIndex(i,proj_.ref_p2d_id,pLandmark->landmark_id);//在Frame中索引.
                                    if(proj_.tracking_state == TRACK_STEREO2MONO||proj_.tracking_state == TRACK_STEREO2STEREO)
                                    {
                                        //pLandmark->setEstimatedPosition(pRefKF->);//这里认为参考的关键帧位姿已经优化完毕.可以直接根据坐标变换关系计算Landmark位置初始估计.
                                        //pLandmark->setTriangulated();//三角化成功.
                                        auto v4d = triangulatePoint(pRefKF->disps_vv.at(i).at(proj_.ref_p2d_id),pRefKF->p2d_vv.at(i).at(proj_.ref_p2d_id).x,
                                                                    pRefKF->p2d_vv.at(i).at(proj_.ref_p2d_id).y,
                                                                    0.12,//DEBUG ONLY!b=0.12
                                                         camfx,camfy,camcx,camcy
                                                         );
                                        //v4d = pInitialEstimate_output->at(Symbol('X',pFrame->getLastKFID()*cam_count + i)).cast<Pose3>().matrix().inverse() * v4d;
                                        //v4d = pRefKF->getFiAndXiArray().second.at(i).matrix().inverse() * v4d;//这里get到的只是相对位置.

                                        v4d = (pRefKF->getFiAndXiArray().second.at(i).matrix()*pRefKF->getFiAndXiArray().first.matrix()).inverse() * v4d;
                                        pLandmark->setEstimatedPosition(Point3(v4d[0],v4d[1],v4d[2]));
                                    }
                                    else if(proj_.tracking_state == TRACK_MONO2STEREO)
                                    {//这种比较特殊.暂时认为当前帧位姿 == 最后一个被优化的帧位姿,进行三角化;这种误差肯定是比上一种情况要大.
                                        //pLandmark->setEstimatedPosition(p);
                                        //pLandmark->setTriangulated();
                                        auto v4d = triangulatePoint(proj_.disp,proj_.current_frame_p2d.x,
                                                                    proj_.current_frame_p2d.y,
                                                                    0.12,//DEBUG ONLY!b=0.12
                                                         camfx,camfy,camcx,camcy
                                                         );
                                        //v4d = pInitialEstimate_output->at(Symbol('X',pFrame->getLastKFID()*cam_count + i)).cast<Pose3>().matrix().inverse() * v4d;
                                        auto fixi_ = this->reproj_db.frameTable.query(pFrame->frame_id - 1)->getFiAndXiArray();
                                        v4d = (fixi_.second.at(i).matrix()*fixi_.first.matrix()).inverse() * v4d;
                                        pLandmark->setEstimatedPosition(Point3(v4d[0],v4d[1],v4d[2]));
                                    }
                                    else if(proj_.tracking_state == TRACK_MONO2MONO)//这种三角化最复杂.应该放到一个队列里去.判断是否能够三角化.实用意义不大.
                                    {
                                        //TriangulationQueue.push_back( std::make_tuple<pLandmark->landmark_id,pFrame->frame_id,ref_kf_id)//加入队列.等待处理.
                                        //在当前帧优化成功之后,检查对极约束是否满足,夹角是否足够三角化....
                                        //pLandmark->setEstimatedPosition();
                                        LOG(WARNING)<<"Track mono2mono not implemented yet!"<<endl;
                                    }
                                    pLandmark->ObservedByFrameIDSet.insert(pFrame->frame_id);//当前帧id加入观测关系.
                                    pLandmark->ObservedByFrameIDSet.insert(pRefKF->frame_id);//追踪的KF也加入.



        //                                ObservationInfo obs_info;
        //                                obs_info.observed_by_frame_id = pFrame->frame_id;
        //                                obs_info.relative_p2d_index = ;//TODO:这块逻辑整理下?

        //                                pLandmark->vObservationInfo.push_back();
                                }
                                else
                                {//维护landmark.
                                    //ObservationInfo obs_info;....//这块用到的时候设计一下.
                                    auto pLandmark = this->reproj_db.landmarkTable.query(
                                                pRefKF->getLandmarkIDByCamIndexAndp2dIndex(i,proj_.ref_p2d_id)
                                                );
                                    pLandmark->ObservedByFrameIDSet.insert(pFrame->frame_id);
                                }
                            }
                            else
                            {
                                trackingState.tracking_avail_failed_count_of_each_cam.at(i).failed+=1;
                            }
                        }
                        //* 可选 加入RelationTable,并更新对应的索引关系.
                    }
                    else
                    {//防止出现空结构无法访问.
                        //TODO.
                        trackingState.tracking_avail_failed_count_of_each_cam.at(i).failed+=1;
                    }
                }
            }
        }
    }

    void analyzeTrackingState(StatisticOfTrackingState& ts,bool& need_KF_output,bool& state_good_output)
    {
        need_KF_output = false;
        state_good_output = false;
        LOG(WARNING)<<"In analyzeTrackingState():got a new state."<<endl;
        int cam_count = ts.tracking_avail_failed_count_of_each_cam.size();
        int total_avail_pts = 0;
        int total_failed_pts = 0;
        for(int i = 0;i<cam_count;i++)
        {
            total_avail_pts  += ts.tracking_avail_failed_count_of_each_cam.at(i).avail;
            total_failed_pts +=ts.tracking_avail_failed_count_of_each_cam.at(i).failed;
        }
        LOG(WARNING)<<"total good pts count:"<<total_avail_pts<<";bad pts count:"<<total_failed_pts<<endl;
        if(total_avail_pts*2.0<(total_avail_pts+total_failed_pts))
        {
            state_good_output = false;//只有33%的点是好点
            need_KF_output = true;
        }
        else
        {
            state_good_output = true;
            if(total_avail_pts*1.3<(total_avail_pts+total_failed_pts))
            {
                need_KF_output = true;
            }
            else
            {
                need_KF_output = false;
            }
        }
    }
    void insertAFrameIntoSlidingWindow(shared_ptr<Frame> pCurrentFrame,bool force_kf)
    //pCurrentFrame:即将插入的帧.
    //force_kf:外部控制必须创建KF.
    {
        ScopeTimer timer__("insertFrameIntoSlidingWindow()");
        StatisticOfTrackingState track_state;
        trackAndKeepReprojectionDBForFrame(pCurrentFrame,track_state);
        this->current_track_state = track_state;
        bool track_good,needKF;
        analyzeTrackingState(current_track_state,needKF,track_good);
        if(!track_good)
        {
            LOG(ERROR)<<"[WARNING]Track state poor!"<<endl;
        }
        if((!needKF) &&(!force_kf))
        {//普通帧
            ScopeTimer timer__("ordinary frame");
            LOG(WARNING)<<"NeedKF is false;create a simple frame."<<endl;
            shared_ptr<Values> pInitialEstimate;
            shared_ptr<NonlinearFactorGraph> pLocalGraph = this->reproj_db.generateLocalGraphByFrameID(pCurrentFrame->frame_id,pInitialEstimate);
            LOG(WARNING)<<"Generated graph for frame:"<<pCurrentFrame->frame_id<<endl;
            LOG(WARNING)<<"KFid list:"<<serializeIntVec(getInWindKFidVec())<<";"<<endl;
            this->optimizeFactorGraph(pLocalGraph,pInitialEstimate);
        }
        else
        {//创建关键帧
            upgradeOrdinaryFrameToKeyFrameStereos(pCurrentFrame);//升级.
            auto& pCurrentKF = pCurrentFrame;
            this->KF_id_queue.push_back(pCurrentKF->frame_id);
            //第四步 创建局部优化图 第一次优化.
            shared_ptr<Values> pLocalInitialEstimate,pLocalRes;
            shared_ptr<NonlinearFactorGraph> pLocalGraph = this->reproj_db.generateLocalGraphByFrameID(pCurrentKF->frame_id,pLocalInitialEstimate);//优化当前帧.
            if(pCurrentKF->frame_id == 0)
            {
                LOG(INFO)<<"First keyframe inserted!"<<endl;
                return;
            }
            cout<<"pLocalGraph.size():"<<pLocalGraph->size()<<endl;
            pLocalRes = optimizeFactorGraph(pLocalGraph,pLocalInitialEstimate);//TODO:这种"优化" 可以考虑多线程实现.
            //优化这个图.
            //第五步 对当前帧,跟踪滑窗里的所有关键帧(地图点向当前帧估计位置重投影).创建新优化图. 这个可以放到后台运行.
            shared_ptr<Values> pSWRes,pSWInitialEstimate;
            shared_ptr<NonlinearFactorGraph> pSlidingWindGraph = this->reproj_db.generateCurrentGraphByKFIDVector(this->getInWindKFidVec(),pSWInitialEstimate);
            cout<<"pSlidingWindowGraph.size():"<<pSlidingWindGraph->size()<<endl;
            LOG(INFO)<<"Generated graph for frame:"<<pCurrentKF->frame_id<<endl;
            LOG(INFO)<<"optimizing kf graph,kf id list:";
            for(auto kfid:this->getInWindKFidVec())
            {
                LOG(INFO)<<kfid;
            }
            LOG(INFO)<<";"<<endl;
            pSWRes = optimizeFactorGraph(pSlidingWindGraph,pSWInitialEstimate,30);
            //第六步 第二次优化.

            //第七步 进行Marginalize,分析优化图并选择要舍弃的关键帧和附属的普通帧,抛弃相应的信息.
            int toMargKFID = this->proposalMarginalizationKF(pCurrentKF->frame_id);
            if(toMargKFID < 0)//无需marg.
            {
                LOG(INFO)<<"deque not full, skip marginalization.return."<<endl;
                return;
            }
            LOG(INFO)<<"Will marginalize kf:"<<toMargKFID<<endl;
            shared_ptr<Frame> pToMarginalizeKF = this->getFrameByID(toMargKFID);
            if(pToMarginalizeKF!= nullptr)
            {
                //TODO:对它的每一个从属OrdinaryFrame进行递归删除.
                if(toMargKFID == this->KF_id_queue.back())
                {
                    this->KF_id_queue.pop_back();
                }
                else if(toMargKFID == this->KF_id_queue.front())
                {
                    this->KF_id_queue.pop_front();
                }
                else
                {
                    LOG(ERROR)<<"error in proposalMarginalizationKF()"<<endl;
                    exit(-1);
                }
                removeKeyFrameAndItsProperties(pToMarginalizeKF);
                removeOrdinaryFrame(pToMarginalizeKF);
            }
        }

    }
    void insertKFintoSlidingWindow(shared_ptr<Frame> pCurrentKF)
    {
        ScopeTimer timer__("insertKFintoSlidingWindow()");
        //第一步 跟踪特征点,创建关联关系.
        //第二步 建立地图点跟踪关系 根据结果集维护数据库.
        StatisticOfTrackingState track_state;
        trackAndKeepReprojectionDBForFrame(pCurrentKF,track_state);
        this->current_track_state = track_state;
        //第三步 将当前普通帧升级成一个关键帧.
        //bool create_kf_success;
        upgradeOrdinaryFrameToKeyFrameStereos(pCurrentKF);//升级.
        this->KF_id_queue.push_back(pCurrentKF->frame_id);
        //第四步 创建局部优化图 第一次优化.
        //method<1>.pnp初始位置估计.
        //  stage<1>.选取最优相机组,估计初始位置.
        //method<2>.继承上一帧初始位置.直接图优化和上一帧的关系.
//        for(int i = 0;i<cam_count;i++)
//        {//创建对应的X,L;插入初始值(X位姿估计在上一个估计点;L位置估计在对应X的坐标变换后位置);Between Factor<>,每次对滑窗里最早的帧约束位置关系.
//            int pose_index = x*pCurrentKF->frame_id + i;
//            localGraph.emplace_shared<Pose3>(...);
//            localGraph.add();
//            localInitialEstimate.insert(...);
//        }


        shared_ptr<Values> pLocalInitialEstimate,pLocalRes;
        shared_ptr<NonlinearFactorGraph> pLocalGraph = this->reproj_db.generateLocalGraphByFrameID(pCurrentKF->frame_id,pLocalInitialEstimate);//优化当前帧.
        if(pCurrentKF->frame_id == 0)
        {
            LOG(INFO)<<"First keyframe inserted!"<<endl;
            return;
        }
        cout<<"pLocalGraph.size():"<<pLocalGraph->size()<<endl;
        pLocalRes = optimizeFactorGraph(pLocalGraph,pLocalInitialEstimate);//TODO:这种"优化" 可以考虑多线程实现.
        //优化这个图.
        //第五步 对当前帧,跟踪滑窗里的所有关键帧(地图点向当前帧估计位置重投影).创建新优化图.

        shared_ptr<Values> pSWRes,pSWInitialEstimate;
        shared_ptr<NonlinearFactorGraph> pSlidingWindGraph = this->reproj_db.generateCurrentGraphByKFIDVector(this->getInWindKFidVec(),pSWInitialEstimate);
        cout<<"pSlidingWindowGraph.size():"<<pSlidingWindGraph->size()<<endl;
        LOG(INFO)<<"Generated graph for frame:"<<pCurrentKF->frame_id<<endl;
        LOG(INFO)<<"optimizing kf graph,kf id list:";
        for(auto kfid:this->getInWindKFidVec())
        {
            LOG(INFO)<<kfid;
        }
        LOG(INFO)<<";"<<endl;
        pSWRes = optimizeFactorGraph(pSlidingWindGraph,pSWInitialEstimate);
        //第六步 第二次优化.

        //第七步 进行Marginalize,分析优化图并选择要舍弃的关键帧和附属的普通帧,抛弃相应的信息.
        int toMargKFID = this->proposalMarginalizationKF(pCurrentKF->frame_id);
        if(toMargKFID < 0)//无需marg.
        {
            LOG(INFO)<<"deque not full, skip marginalization.return."<<endl;
            return;
        }
        LOG(INFO)<<"Will marginalize kf:"<<toMargKFID<<endl;
        shared_ptr<Frame> pToMarginalizeKF = this->getFrameByID(toMargKFID);
        if(pToMarginalizeKF!= nullptr)
        {
            //TODO:对它的每一个从属OrdinaryFrame进行递归删除.
            if(toMargKFID == this->KF_id_queue.back())
            {
                this->KF_id_queue.pop_back();
            }
            else if(toMargKFID == this->KF_id_queue.front())
            {
                this->KF_id_queue.pop_front();
            }
            else
            {
                LOG(ERROR)<<"error in proposalMarginalizationKF()"<<endl;
                exit(-1);
            }
            removeKeyFrameAndItsProperties(pToMarginalizeKF);
            removeOrdinaryFrame(pToMarginalizeKF);
        }
    }

    void insertOrdinaryFrameintoSlidingWindow(shared_ptr<Frame> pCurrentFrame)
    //普通帧和关键帧的区别是:普通帧没有附属landmark properties;普通帧与普通帧之间不考虑关联追踪.
    {
        ScopeTimer timer__("insertOrdinaryFrameintoSlidingWindow()");
        //第一步 跟踪特征点.
        //第二步 建立地图点跟踪关系. 修改/创建LandmarkProperties.
        StatisticOfTrackingState track_state;
        trackAndKeepReprojectionDBForFrame(pCurrentFrame,track_state);
        this->current_track_state = track_state;
        //第三步 创建局部优化图.第一次优化.
        shared_ptr<Values> pInitialEstimate;
        shared_ptr<NonlinearFactorGraph> pLocalGraph = this->reproj_db.generateLocalGraphByFrameID(pCurrentFrame->frame_id,pInitialEstimate);
        LOG(WARNING)<<"Generated graph for frame:"<<pCurrentFrame->frame_id<<endl;
        LOG(WARNING)<<"KFid list:"<<serializeIntVec(getInWindKFidVec())<<";"<<endl;
        this->optimizeFactorGraph(pLocalGraph,pInitialEstimate);
    }

    void removeOrdinaryFrame(shared_ptr<Frame>)
    {
        //TODO.
    }
    void removeKeyFrameAndItsProperties(shared_ptr<Frame>)
    {
        //TODO.
    }
    inline int getOrdinaryFrameSize();
    inline int getKFSize();
    vector<int> getInWindKFidVec()
    {
        vector<int> retval;
        for(int u:this->KF_id_queue)
        {
            retval.push_back(u);
        }
        return retval;
    }
    int proposalMarginalizationKF(int currentFrameID)//提议一个应该被marg的关键帧.
    {
        auto pCurrentFrame = this->getFrameByID(currentFrameID);
        deque<int> currentInWindKFList = this->getKFidQueue();
        if(currentInWindKFList.size() < this->max_kf_count)
        {
            return -1;
        }
        vector<double> v_mean_disp;
        for(auto iter = currentInWindKFList.begin();iter!=currentInWindKFList.end();++iter)
        {
            int kf_id = *iter;
            //vector<shared_ptr<Frame> > v_relative_frames = this->reproj_db.frameTable.queryByRefKFID(kf_id);//查询和他有关联的帧.
            //for(auto& pF:v_relative_frames)
            //{//判断marg哪个关键帧最合适.
            //    //step<1>.计算帧间平均视差.
            //    double mean_disp = calcMeanDispBetween2Frames(pCurrentFrame,pF);
            //}
            for(auto proj_rec_iter = pCurrentFrame->reproj_map.begin();proj_rec_iter!=pCurrentFrame->reproj_map.end();++proj_rec_iter)
            {
                int ref_kf_id = proj_rec_iter->first;
                auto& reproj_relation = proj_rec_iter->second;
                double avg_disp = getAverageDisp(reproj_relation,ref_kf_id);
                LOG(WARNING)<<"Avg disp between F"<<currentFrameID<<" and RefKF"<<ref_kf_id<<":"<<avg_disp<<endl;
                v_mean_disp.push_back(avg_disp);
            }
            //step<2>.如果第一帧到当前帧平均视差 与 最后一帧到当前帧平均视差 比值<2.0: marg最后一个关键帧
            //    (一直不动.就不要创建新的.否则会一直累计误差.)

            //step<3>.否则,marg第一个关键帧.
        }
        LOG(WARNING)<<"Current frame id:"<<currentFrameID<<",kfid list:"<<serializeIntVec(getInWindKFidVec())<<";"<<endl;
        if(v_mean_disp[0]<2* (v_mean_disp.back()) )
        {
            LOG(WARNING)<<"will marginalize first kf."<<endl;
            return currentInWindKFList.front();
        }
        LOG(WARNING)<<"will marginalize last kf."<<endl;
        return currentInWindKFList.back();
    }
};

inline double get_dist_between_2_p2dT(const cv::Point2f& p1,const cv::Point2f& p2)
{
    return sqrt(pow(p1.x - p2.x,2)+pow(p1.y-p2.y,2));
}
double SlidingWindow::getAverageDisp(const ReprojectionRecordT& reproj_info,int ref_kf_id)
{//计算平均像素位置变化
    auto pRefKF = this->getFrameByID(ref_kf_id);
    vector<double> dist_v;
    for(int cam_i = 0;cam_i<reproj_info.size();cam_i++)
    {
        for(int track_j = 0;track_j<reproj_info.at(cam_i).size();track_j++)
        {
            auto& reproj_ = reproj_info.at(cam_i).at(track_j);
            const p2dT& current_p2d = reproj_.current_frame_p2d;
            const p2dT& ref_p2d = pRefKF->p2d_vv.at(cam_i).at(reproj_.ref_p2d_id);
            dist_v.push_back(get_dist_between_2_p2dT(current_p2d,ref_p2d));
        }
    }
    double avg = 0;
    if(dist_v.size() == 0)
    {
        LOG(INFO)<<"Track failed!!"<<endl;
        return 0;
    }
    for(double& dist:dist_v)
    {
        avg+=dist;
    }
    avg/=dist_v.size();
    return avg;//TODO:fill in this.
}






}
#endif