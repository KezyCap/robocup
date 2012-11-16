#ifndef MRLAGENT_H
#define MRLAGENT_H

#include "ApproximatorInterface.h"
#include "RLearningInterface.h"
#include "DictionaryApproximator.h"
#include "RLAgent.h"

#include <vector>

class MRLAgent: public RLAgent
{
public:
    virtual void initialiseAgent(int numberOfInputs, int numberOfOutputs, int numberOfHiddens);

    virtual void setParameters(float alpha=0.1f, float beta=0.5, float gamma=0.9f, float lambda=0.9f,int learningIterations=1);

    virtual int getAction(vector<float> observations);//Must return integer between 0 and numberOfOutputs-1

    virtual void giveReward(float reward);

    virtual void doLearning();

    virtual void saveAgent(string agentName);

    virtual void loadAgent(string agentName);
    float giveMotivationReward();
    float wundtFunction();

};

#endif // MRLAGENT_H