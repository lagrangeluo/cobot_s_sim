
//* ROS action server */
#include <ros/ros.h>
#include <iostream>
#include <actionlib/server/simple_action_server.h>
#include <control_msgs/FollowJointTrajectoryAction.h>
#include <std_msgs/Float32MultiArray.h>
#include <sensor_msgs/JointState.h>

//RM Robot msg
#include <rm_75_msgs/JointPos.h>

/* 三次样条插补 */
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "cubicSpline.h"

/* 使用变长数组 */
#include <vector>
#include <algorithm>

#include<std_msgs/Bool.h>

using namespace std;
vector<double> time_from_start_;
vector<double> p_joint1_;
vector<double> p_joint2_;
vector<double> p_joint3_;
vector<double> p_joint4_;
vector<double> p_joint5_;
vector<double> p_joint6_;
/***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
vector<double> p_joint7_;
/***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

vector<double> v_joint1_;
vector<double> v_joint2_;
vector<double> v_joint3_;
vector<double> v_joint4_;
vector<double> v_joint5_;
vector<double> v_joint6_;
/***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
vector<double> v_joint7_;
/***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

vector<double> a_joint1_;
vector<double> a_joint2_;
vector<double> a_joint3_;
vector<double> a_joint4_;
vector<double> a_joint5_;
vector<double> a_joint6_;
/***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
vector<double> a_joint7_;
/***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

/* 存储的结构体 p2*/
struct vel_data
{
    int vector_len;    //toatal num
    int vector_cnt;    //current num
};

/* 数据收发结构体 */
struct vel_data p2;

/* action 服务端声明 */
typedef actionlib::SimpleActionServer<control_msgs::FollowJointTrajectoryAction> Server;

/* 初始化输入输出速度加速度 */
double acc = 0, vel = 0;
double x_out = 0, y_out = 0;

/* 判断路点数据是否改变 */
bool point_changed = false;

/*三次样条插值后周期*/
double rate = 0.020;    // 5ms

// ros::Publisher joint_pub;         //Simulation robot
ros::Publisher target_pub;        //Real robot
ros::Publisher pub_getArmStateTimerSwitch;

/* 三次样条无参构造 */
cubicSpline::cubicSpline()
{
}
/* 析构 */
cubicSpline::~cubicSpline()
{
    releaseMem();
}
/* 初始化参数 */
void cubicSpline::initParam()
{
    x_sample_ = y_sample_ = M_ = NULL;
    sample_count_ = 0;
    bound1_ = bound2_ = 0;
}
/* 释放参数 */
void cubicSpline::releaseMem()
{
    delete x_sample_;
    delete y_sample_;
    delete M_;

    initParam();
}
/* 加载关节位置数组等信息 */
bool cubicSpline::loadData(double *x_data, double *y_data, int count, double bound1, double bound2, BoundType type)
{
    if ((NULL == x_data) || (NULL == y_data) || (count < 3) || (type > BoundType_Second_Derivative) || (type < BoundType_First_Derivative))
    {
        return false;
    }
    initParam();

    x_sample_ = new double[count];
    y_sample_ = new double[count];
    M_        = new double[count];
    sample_count_ = count;

    memcpy(x_sample_, x_data, sample_count_*sizeof(double));
    memcpy(y_sample_, y_data, sample_count_*sizeof(double));

    bound1_ = bound1;
    bound2_ = bound2;

    return spline(type);
}
/* 计算样条插值 */
bool cubicSpline::spline(BoundType type)
{
    if ((type < BoundType_First_Derivative) || (type > BoundType_Second_Derivative))
    {
        return false;
    }

    //  追赶法解方程求二阶偏导数
    double f1=bound1_, f2=bound2_;

    double *a=new double[sample_count_];                //  a:稀疏矩阵最下边一串数
    double *b=new double[sample_count_];                //  b:稀疏矩阵最中间一串数
    double *c=new double[sample_count_];                //  c:稀疏矩阵最上边一串数
    double *d=new double[sample_count_];

    double *f=new double[sample_count_];

    double *bt=new double[sample_count_];
    double *gm=new double[sample_count_];

    double *h=new double[sample_count_];

    for(int i=0;i<sample_count_;i++)
        b[i]=2;                                //  中间一串数为2
    for(int i=0;i<sample_count_-1;i++)
        h[i]=x_sample_[i+1]-x_sample_[i];      // 各段步长
    for(int i=1;i<sample_count_-1;i++)
        a[i]=h[i-1]/(h[i-1]+h[i]);
    a[sample_count_-1]=1;

    c[0]=1;
    for(int i=1;i<sample_count_-1;i++)
        c[i]=h[i]/(h[i-1]+h[i]);

    for(int i=0;i<sample_count_-1;i++)
        f[i]=(y_sample_[i+1]-y_sample_[i])/(x_sample_[i+1]-x_sample_[i]);

    for(int i=1;i<sample_count_-1;i++)
        d[i]=6*(f[i]-f[i-1])/(h[i-1]+h[i]);

    //  追赶法求解方程
    if(BoundType_First_Derivative == type)
    {
        d[0]=6*(f[0]-f1)/h[0];
        d[sample_count_-1]=6*(f2-f[sample_count_-2])/h[sample_count_-2];

        bt[0]=c[0]/b[0];
        for(int i=1;i<sample_count_-1;i++)
            bt[i]=c[i]/(b[i]-a[i]*bt[i-1]);

        gm[0]=d[0]/b[0];
        for(int i=1;i<=sample_count_-1;i++)
            gm[i]=(d[i]-a[i]*gm[i-1])/(b[i]-a[i]*bt[i-1]);

        M_[sample_count_-1]=gm[sample_count_-1];
        for(int i=sample_count_-2;i>=0;i--)
            M_[i]=gm[i]-bt[i]*M_[i+1];
    }
    else if(BoundType_Second_Derivative == type)
    {
        d[1]=d[1]-a[1]*f1;
        d[sample_count_-2]=d[sample_count_-2]-c[sample_count_-2]*f2;

        bt[1]=c[1]/b[1];
        for(int i=2;i<sample_count_-2;i++)
            bt[i]=c[i]/(b[i]-a[i]*bt[i-1]);

        gm[1]=d[1]/b[1];
        for(int i=2;i<=sample_count_-2;i++)
            gm[i]=(d[i]-a[i]*gm[i-1])/(b[i]-a[i]*bt[i-1]);

        M_[sample_count_-2]=gm[sample_count_-2];
        for(int i=sample_count_-3;i>=1;i--)
            M_[i]=gm[i]-bt[i]*M_[i+1];

        M_[0]=f1;
        M_[sample_count_-1]=f2;
    }
    else
        return false;

    delete a;
    delete b;
    delete c;
    delete d;
    delete gm;
    delete bt;
    delete f;
    delete h;

    return true;
}
/* 得到速度和加速度数组 */
bool cubicSpline::getYbyX(double &x_in, double &y_out)
{
    int klo,khi,k;
    klo=0;
    khi=sample_count_-1;
    double hh,bb,aa;

    //  二分法查找x所在区间段
    while(khi-klo>1)
    {
        k=(khi+klo)>>1;
        if(x_sample_[k]>x_in)
            khi=k;
        else
            klo=k;
    }
    hh=x_sample_[khi]-x_sample_[klo];

    aa=(x_sample_[khi]-x_in)/hh;
    bb=(x_in-x_sample_[klo])/hh;

    y_out=aa*y_sample_[klo]+bb*y_sample_[khi]+((aa*aa*aa-aa)*M_[klo]+(bb*bb*bb-bb)*M_[khi])*hh*hh/6.0;

    //test
    acc = (M_[klo]*(x_sample_[khi]-x_in) + M_[khi]*(x_in - x_sample_[klo])) / hh;
    vel = M_[khi]*(x_in - x_sample_[klo]) * (x_in - x_sample_[klo]) / (2 * hh)
          - M_[klo]*(x_sample_[khi]-x_in) * (x_sample_[khi]-x_in) / (2 * hh)
          + (y_sample_[khi] - y_sample_[klo])/hh
          - hh*(M_[khi] - M_[klo])/6;
    //printf("[---位置、速度、加速度---]");
    //printf("%0.9f, %0.9f, %0.9f\n",y_out, vel, acc);
    //test end

    return true;
}

/* 收到action的goal后调用的回调函数 */
void execute(const control_msgs::FollowJointTrajectoryGoalConstPtr& goal, Server* as) {

    /* move_group规划的路径包含的路点个数 */
    /* 规划的路点数目 */
    int i = 0;
    int point_num = goal->trajectory.points.size();
    ROS_INFO("First Move_group give us %d points",point_num);
    point_changed = false;

    std_msgs::Bool timerSwitch;
    
        /***********Start gubs 2021/9/16 修复Moveit在同一位姿重复规划执行导致rm_contorl异常停止的Bug***********/
    if  (point_num > 3) //判断当moveit规划的路点数大于3时为有效规划并进行三次样条插值；如果路点数小于3则判断为在同一位姿进行的规划,不执行操作
    {
        /* 各个关节位置 */
        double p_joint1[point_num];
        double p_joint2[point_num];
        double p_joint3[point_num];
        double p_joint4[point_num];
        double p_joint5[point_num];
        double p_joint6[point_num];
        /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
        double p_joint7[point_num];
        /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

            /* 各个关节速度 */
        double v_joint1[point_num];
        double v_joint2[point_num];
        double v_joint3[point_num];
        double v_joint4[point_num];
        double v_joint5[point_num];
        double v_joint6[point_num];
        /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
        double v_joint7[point_num];
        /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

        /* 各个关节加速度 */
        double a_joint1[point_num];
        double a_joint2[point_num];
        double a_joint3[point_num];
        double a_joint4[point_num];
        double a_joint5[point_num];
        double a_joint6[point_num];
        /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
        double a_joint7[point_num];
        /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

        /* 时间数组 */
        double time_from_start[point_num];

        for (int i = 0; i < point_num; i++) {
            p_joint1[i] = goal->trajectory.points[i].positions[0];
            p_joint2[i] = goal->trajectory.points[i].positions[1];
            p_joint3[i] = goal->trajectory.points[i].positions[2];
            p_joint4[i] = goal->trajectory.points[i].positions[3];
            p_joint5[i] = goal->trajectory.points[i].positions[4];
            p_joint6[i] = goal->trajectory.points[i].positions[5];
            /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
            p_joint7[i] = goal->trajectory.points[i].positions[6];
            /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

            v_joint1[i] = goal->trajectory.points[i].velocities[0];
            v_joint2[i] = goal->trajectory.points[i].velocities[1];
            v_joint3[i] = goal->trajectory.points[i].velocities[2];
            v_joint4[i] = goal->trajectory.points[i].velocities[3];
            v_joint5[i] = goal->trajectory.points[i].velocities[4];
            v_joint6[i] = goal->trajectory.points[i].velocities[5];
            /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
            v_joint7[i] = goal->trajectory.points[i].velocities[6];
            /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

            a_joint1[i] = goal->trajectory.points[i].accelerations[0];
            a_joint2[i] = goal->trajectory.points[i].accelerations[1];
            a_joint3[i] = goal->trajectory.points[i].accelerations[2];
            a_joint4[i] = goal->trajectory.points[i].accelerations[3];
            a_joint5[i] = goal->trajectory.points[i].accelerations[4];
            a_joint6[i] = goal->trajectory.points[i].accelerations[5];
            /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
            a_joint7[i] = goal->trajectory.points[i].accelerations[6];
            /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

            time_from_start[i] = goal->trajectory.points[i].time_from_start.toSec();
        }

        // 实例化样条
        cubicSpline spline;
        double max_time = time_from_start[point_num-1];    // 规划时间的最后一个
        ROS_INFO("Second Move_group max_time is %f ",max_time);
        time_from_start_.clear();   // 清空

        // joint1
        if (spline.loadData(time_from_start, p_joint1, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
        {
            p_joint1_.clear();
            v_joint1_.clear();
            a_joint1_.clear();
            x_out = -rate;
            while(x_out < max_time) {
                x_out += rate;
                spline.getYbyX(x_out, y_out);
                time_from_start_.push_back(x_out);  // 将新的时间存储，只需操作一次即可
                p_joint1_.push_back(y_out);
                v_joint1_.push_back(vel);
                a_joint1_.push_back(acc);
            }

            // joint2
            if (spline.loadData(time_from_start, p_joint2, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
            p_joint2_.clear();
            v_joint2_.clear();
            a_joint2_.clear();
            x_out = -rate;
            while(x_out < max_time) {
                x_out += rate;
                spline.getYbyX(x_out, y_out);
                p_joint2_.push_back(y_out);
                v_joint2_.push_back(vel);
                a_joint2_.push_back(acc);
            }

            // joint3
            if (spline.loadData(time_from_start, p_joint3, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
            {
                p_joint3_.clear();
                v_joint3_.clear();
                a_joint3_.clear();
                x_out = -rate;
                while(x_out < max_time) {
                    x_out += rate;
                    spline.getYbyX(x_out, y_out);
                    p_joint3_.push_back(y_out);
                    v_joint3_.push_back(vel);
                    a_joint3_.push_back(acc);
                }

                // joint4
                if (spline.loadData(time_from_start, p_joint4, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
                {
                    p_joint4_.clear();
                    v_joint4_.clear();
                    a_joint4_.clear();
                    x_out = -rate;
                    while(x_out < max_time) {
                        x_out += rate;
                        spline.getYbyX(x_out, y_out);
                        p_joint4_.push_back(y_out);
                        v_joint4_.push_back(vel);
                        a_joint4_.push_back(acc);
                    }

                    // joint5
                    if (spline.loadData(time_from_start, p_joint5, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
                    {
                        p_joint5_.clear();
                        v_joint5_.clear();
                        a_joint5_.clear();
                        x_out = -rate;
                        while(x_out < max_time) {
                            x_out += rate;
                            spline.getYbyX(x_out, y_out);
                            p_joint5_.push_back(y_out);
                            v_joint5_.push_back(vel);
                            a_joint5_.push_back(acc);
                        }

                        // joint6
                        if (spline.loadData(time_from_start, p_joint6, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
                        {
                            p_joint6_.clear();
                            v_joint6_.clear();
                            a_joint6_.clear();
                            x_out = -rate;
                            while(x_out < max_time) {
                                x_out += rate;
                                spline.getYbyX(x_out, y_out);
                                p_joint6_.push_back(y_out);
                                v_joint6_.push_back(vel);
                                a_joint6_.push_back(acc);
                            }

                            /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
                            // joint7
                            if (spline.loadData(time_from_start, p_joint7, point_num, 0, 0, cubicSpline::BoundType_First_Derivative))
                            {
                                p_joint7_.clear();
                                v_joint7_.clear();
                                a_joint7_.clear();
                                x_out = -rate;
                                while(x_out < max_time) {
                                    x_out += rate;
                                    spline.getYbyX(x_out, y_out);
                                    p_joint7_.push_back(y_out);
                                    v_joint7_.push_back(vel);
                                    a_joint7_.push_back(acc);
                                }
                                /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/

                                //control_msgs::FollowJointTrajectoryFeedback feedback;
                                //feedback = NULL;
                                //as->publishFeedback(feedback);
                                ROS_INFO("Now We get all tarjectory num %ld", time_from_start_.size());

                                p2.vector_len = time_from_start_.size();
                                p2.vector_cnt = 0;

                                timerSwitch.data = true;
                                pub_getArmStateTimerSwitch.publish(timerSwitch);

                                point_changed = true;

                                /***********Start gubs 2021/9/15 等待定时器将数据取出并发送完***********/
                                while(point_changed)
                                {
                                    ros::WallDuration(0.05).sleep();
                                }
                                ros::WallDuration(0.5).sleep();
                                /***********End  gubs 2021/9/15 等待定时器将数据取出并发送完***********/
                                
                            }

                        }

                    }

                }

            }

        }

    }
    as->setSucceeded();
    timerSwitch.data = false;
    pub_getArmStateTimerSwitch.publish(timerSwitch);

    //发送规划角度，防止真实机械臂连不上
//    sensor_msgs::JointState joint_state;
//    joint_state.header.stamp = ros::Time::now();
//    joint_state.name.resize(6);
//    joint_state.position.resize(6);
//    joint_state.name[0] = "joint1";
//    joint_state.name[1] = "joint2";
//    joint_state.name[2] = "joint3";
//    joint_state.name[3] = "joint4";
//    joint_state.name[4] = "joint5";
//    joint_state.name[5] = "joint6";

//    for(i=0;i<6;i++)
//    {
//        joint_state.position[i] = goal->trajectory.points[point_num-1].positions[i];
//    }

//    joint_pub.publish(joint_state);
}

void timer_callback(const ros::TimerEvent)
{
    rm_75_msgs::JointPos msg;

    if(point_changed)
    {
        if(p2.vector_cnt < p2.vector_len)
        {
            //ROS_INFO("Pos:[%f, %f, %f, %f, %f, %f]",  p_joint1_.at(p2.vector_cnt), p_joint2_.at(p2.vector_cnt), p_joint3_.at(p2.vector_cnt), p_joint4_.at(p2.vector_cnt), p_joint5_.at(p2.vector_cnt), p_joint6_.at(p2.vector_cnt));
            msg.joint[0] = p_joint1_.at(p2.vector_cnt);
            msg.joint[1] = p_joint2_.at(p2.vector_cnt);
            msg.joint[2] = p_joint3_.at(p2.vector_cnt);
            msg.joint[3] = p_joint4_.at(p2.vector_cnt);
            msg.joint[4] = p_joint5_.at(p2.vector_cnt);
            msg.joint[5] = p_joint6_.at(p2.vector_cnt);
            /***********Start gubs 2021/8/13 添加7轴的joint7相关***********/
            msg.joint[6] = p_joint7_.at(p2.vector_cnt);
            /***********End   gubs 2021/8/13 添加7轴的joint7相关***********/
            target_pub.publish(msg);
            p2.vector_cnt++;
        }
        else
        {
            p2.vector_cnt = 0;
            p2.vector_len = 0;
            point_changed = false;
        }
    }
}

/* 主函数主要用于动作订阅和套接字通信 */
int main(int argc, char** argv)
{
    ros::init(argc, argv, "rm_arm_control");
    ros::NodeHandle nh;
    ros::Timer State_Timer;

    //timer,5ms
    State_Timer = nh.createTimer(ros::Duration(0.02), timer_callback);
    // joint_pub = nh.advertise<sensor_msgs::JointState>("/joint_states", 1);
    target_pub = nh.advertise<rm_75_msgs::JointPos>("/rm_driver/JointPos", 200);
    pub_getArmStateTimerSwitch = nh.advertise<std_msgs::Bool>("/rm_driver/GetArmStateTimerSwitch", 200);
    // 定义一个服务器
    Server server(nh, "rm_75/follow_joint_trajectory", boost::bind(&execute, _1, &server), false);
    // 服务器开始运行
    server.start();
    ros::spin();

    return 0;
}
