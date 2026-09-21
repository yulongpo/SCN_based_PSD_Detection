#pragma once

#include "radioai/icd/HQSigMF.hpp"

/**
 * @file Feature.hpp
 * @brief 特征库变更和特征数组的 ICD 定义。
 *
 * FeaturesSet 负责释放嵌套的 items 和每个特征向量的 data，因此这些指针
 * 必须由与该析构约定匹配的 new[] 分配；不要把栈地址或其他所有者的内存交给它。
 */

/// 一项命名特征及其浮点向量属性。
struct FeatureItem {
    std::string name;        //特征名称
    int32_t len;             //特征向量长度
    float* data;             //特征向量值
    float density;
    float rad;
    float weights;
};

/// 一个标签对象下的多项特征。
struct FeaturesPerObj {
    std::string label;       //标签名称
    int32_t num;             //特征向量个数
    FeatureItem* items;      //特征列表
};

/// 特征集合的拥有型容器，析构时递归释放全部嵌套数组。
struct FeaturesSet {
    int32_t num;                 //对象个数
    FeaturesPerObj* features;    //特征列表

    virtual ~FeaturesSet()
    {
        if (features) {
            for (int32_t i = 0; i < num; ++i) {
                FeaturesPerObj& obj = features[i];
                if (obj.items) {
                    for (int32_t j = 0; j < obj.num; ++j) {
                        delete[] obj.items[j].data;
                    }
                    delete[] obj.items;
                }
            }
            delete[] features;
        }
    }
};

typedef HQSigMF AddFeatureReq;     //添加特征请求
typedef HQSigMF AddSigSampleReq;   //添加信号样本请求

/// 按标签和特征名删除一项特征。
struct DelFeatureReq : public radioai::core::ComData {
    std::string label;           //特征标签名称
    std::string name;            //特征名称

    virtual radioai::core::ComData* clone() override
    {
        auto clone_obj = new (std::nothrow) DelFeatureReq;
        if (nullptr == clone_obj)return nullptr;
        *clone_obj = *this;
        return clone_obj;
    }
};

/// 广播特征库新增或删除事件，并共享变更的特征集合。
struct FeatureLibChangedEvent : public radioai::core::ComData {
    int32_t type;                //变更类型{0-新增特征、1-删除特征}
    std::shared_ptr<FeaturesSet> features_set;    //变更的特征集

    virtual radioai::core::ComData* clone() override
    {
        auto clone_obj = new (std::nothrow) FeatureLibChangedEvent;
        if (nullptr == clone_obj)return nullptr;
        *clone_obj = *this;
        return clone_obj;
    }
};
