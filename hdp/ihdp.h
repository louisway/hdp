#ifndef IHDP_H
#define IHDP_H

#include "state.h"


/// implement the Chinese restaurant franchies algorithm with split and merge
class ihdp
{
public:
/// fixed parameters
    hdp_hyperparameter * m_hdp_param;
    int time_interval; // new add with time_interval

/// sampling state
    hdp_state * m_state;

public:
    ihdp();
    virtual ~ihdp();
public:
    void run(const char * directory);
    void run_test(const char * directory);

    void setup_state(const corpus * c,
                     double _eta, int init_topics,
                     hdp_hyperparameter * _hdp_param);
    void setup_state(const corpus * c, 
                     hdp_hyperparameter * _hdp_param);
    void load(char * model_path);
    
    // new add 3 sampler 
    void predict_sampler();
    void hdp_sampler();
    void rejuvenation_sampler();
};

#endif // IHDP_H
