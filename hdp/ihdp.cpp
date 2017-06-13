#include "ihdp.h"
#include "utils.h"
#include <assert.h>

#define VERBOSE true
#define null NULL

#define PERMUTE true
#define PERMUTE_LAG 10
#define TABLE_SAMPLING true
#define NUM_SPLIT_MERGE_TRIAL 15
#define SPLIT_MERGE_MAX_ITER 1

ihdp::ihdp()
{
    //ctor
    m_hdp_param = NULL;
    time_interval = -1;
    m_state = NULL;
}

ihdp::~ihdp()
{
    //dtor
    m_hdp_param = NULL;
    time_interval = -1;

    delete m_state; m_state = NULL;
    time_stack.clear();
}

void ihdp::load(char * model_path) // currently useless
{
    m_state = new hdp_state();
    m_state->load_state_ex(model_path);
}

void ihdp::setup_state(const corpus * c,
                      hdp_hyperparameter * _hdp_param) // this is following the load func
{
    m_hdp_param = _hdp_param;
    time_interval = c->time_interval;
    m_state->setup_state_from_corpus(c);
    m_state->allocate_initial_space();
}

void ihdp::setup_state(const corpus * c,
                      double _eta, int init_topics,
                      hdp_hyperparameter * _hdp_param) // this is the one we are going to use
{

    m_hdp_param = _hdp_param;
    time_interval = c->time_interval;
    time_stack = c->time_stack;
    m_state = new hdp_state();

    m_state->setup_state_from_corpus(c);
    m_state->allocate_initial_space();
    m_state->m_eta = _eta;
    m_state->m_num_topics = init_topics;
    /// set the pointer for the hyper parameters
    assert(m_hdp_param != NULL);
    /// use the means of gamma distribution
    m_state->m_gamma = m_hdp_param->m_gamma_a * m_hdp_param->m_gamma_b;
    m_state->m_alpha = m_hdp_param->m_alpha_a * m_hdp_param->m_alpha_b;
}

void ihdp::run()
{
    //init for the first year
    m_state->HDP_Sampler(time_stack[0] , false);
    for( int inter = 0; iter < m_hdp_param->m_max_iter; iter++)
    {
        m_state->HDP_Sampler(time_stack[0], true);
        //SPLIT AND MERGE WAITED TO BE UPDATED 
    }
    
    // start to ihdp
   
    for (int point = 1; point <= time_interval; ++point )
    {
    }
}
void ihdp::run(const char * directory)
{
    if (m_state->m_num_topics == 0)
        m_state->iterate_gibbs_state(false, PERMUTE, m_hdp_param, TABLE_SAMPLING); //init the state
    else if(m_state->m_num_topics > 0)
        m_state->init_gibbs_state_using_docs();
    else // m_state->m_num_topics < 0
    {
        m_state->m_num_topics = abs(m_state->m_num_topics);
        m_state->init_gibbs_state_with_fixed_num_topics();
    }
    printf("starting with %d topics \n", m_state->m_num_topics);

    double best_likelihood = m_state->joint_likelihood(m_hdp_param);

    bool permute = false;

    /// time part ...
    time_t start, current;
    time (&start);
    double dif;
    ///
    int tot = 0;
    int acc = 0;
    for (int iter = 0; iter < m_hdp_param->m_max_iter; iter++)
    {
        printf("iter = %05d, ", iter);

        if (PERMUTE && (iter > 0) && (iter % PERMUTE_LAG == 0)) permute = true;
        else permute = false;

        m_state->iterate_gibbs_state(true, permute, m_hdp_param, TABLE_SAMPLING);

        double likelihood = m_state->joint_likelihood(m_hdp_param);

        time(&current); dif = difftime (current,start);

        printf("#topics = %04d, #tables = %04d, gamma = %.5f, alpha = %.5f, likelihood = %.5f\n",
                        m_state->m_num_topics, m_state->m_total_num_tables,
                        m_state->m_gamma, m_state->m_alpha, likelihood);

        if (best_likelihood < likelihood)
        {
            best_likelihood = likelihood;
        }

        int num_split=0, num_merge=0, num_trial = 0;
        //if (m_hdp_param->m_split_merge_sampler && iter < m_hdp_param->m_max_iter-1)
        if (m_hdp_param->m_split_merge_sampler && iter < SPLIT_MERGE_MAX_ITER)
        {
            num_trial = NUM_SPLIT_MERGE_TRIAL;
            for (int num = 0; num < num_trial; num++)
            {
                tot ++;
                hdp_state* proposed_state = new hdp_state();
                proposed_state->copy_state(m_state);

                int num_scans = m_hdp_param->m_num_restricted_scans;
                int d0, t0, d1, t1;
                ACTION action = m_state->select_mcmc_move(d0, d1, t0, t1);
                if (action == SPLIT)
                {
                    int k0 = m_state->m_doc_states[d0]->m_table_to_topic[t0];
                    int k1 = m_state->m_num_topics;
                    double prob_split = proposed_state->split_sampling(num_scans, d0, d1, t0, t1);

                    double r = compute_split_ratio(proposed_state, m_state, k0, k1);
                    printf("like.log = %5.2lf, ", r);
                    printf("scan.log = %5.2lf, ", prob_split);
                    r -= prob_split;

                    double u = log(runiform());
                    if (u < r)
                    {
                        acc ++;
                        num_split++;
                        printf("ratio.log = %5.2lf/%5.2lf, split (--- A ---), ", r, u);
                        printf("%d -> %d\n", m_state->m_num_topics, m_state->m_num_topics+1);
                        hdp_state* old_state = m_state;
                        m_state = proposed_state;
                        proposed_state = old_state;
                    }
                    else
                    {
                        printf("ratio.log = %5.2lf, split (--- R ---)\n", r);
                    }
                }
                else // action == MERGE
                {
                    hdp_state* intermediat_state = new hdp_state();
                    intermediat_state->copy_state(m_state);

                    double prob_split = intermediat_state->split_sampling(num_scans, d0, d1, t0, t1, m_state);

                    int k0 = m_state->m_doc_states[d0]->m_table_to_topic[t0];
                    int k1 = m_state->m_doc_states[d1]->m_table_to_topic[t1];
                    proposed_state->merge_two_topics(k0, k1);

                    double r = -compute_split_ratio(m_state, proposed_state, k0, k1);
                    printf("like.log = %5.2lf, ", r);
                    printf("scan.log = %5.2lf, ", prob_split);
                    r += prob_split;

                    double u = log(runiform());
                    if (u < r)
                    {
                        acc++;
                        num_merge++;
                        printf("ratio.log = %5.2lf/%5.2lf, merge (--- A ---), ", r, u);
                        printf("%d -> %d\n", m_state->m_num_topics, m_state->m_num_topics-1);
                        hdp_state* old_state = m_state;
                        m_state = proposed_state;
                        proposed_state = old_state;
                    }
                    else
                    {
                         printf("ratio.log = %5.2lf, merge (--- R ---)\n", r);
                    }
                    delete intermediat_state;
                }
                delete proposed_state;
            }
        }

       fprintf(file, "%d %d %d\n", num_split, num_merge, num_trial);
    }
    fclose(file);

    if (m_hdp_param->m_split_merge_sampler)
        printf("accept rate: %.2lf%%\n", 100.0 * (double)acc/tot);

}

void ihdp::run_test(const char * directory) // currently useless
{
    double old_likelihood = m_state->table_partition_likelihood() + m_state->data_likelihood();
    m_state->iterate_gibbs_state(false, PERMUTE, m_hdp_param, TABLE_SAMPLING); //init the state
    printf("starting with %d topics \n", m_state->m_num_topics);

    char name[500];
    sprintf(name, "%s/test.log", directory);
    FILE* file = fopen(name, "w");
    fprintf(file, "time iter num.topics num.tables likelihood\n");

    time_t start, current;
    time (&start);
    double dif;

    bool permute = false;
    double likelihood, best_likelihood;
    for (int iter = 0; iter < m_hdp_param->m_max_iter; iter++)
    {
        printf("iter = %05d, ", iter);

        if (PERMUTE && (iter > 0) && (iter % PERMUTE_LAG == 0)) permute = true;
        else permute = false;

        m_state->iterate_gibbs_state(true, permute, m_hdp_param, TABLE_SAMPLING);
        likelihood = m_state->joint_likelihood(m_hdp_param) - old_likelihood;
        time(&current); dif = difftime (current,start);
        printf("#topics = %04d, #tables = %04d, likelihood = %.5f\n",
                m_state->m_num_topics, m_state->m_total_num_tables, likelihood);
        fprintf(file, "%8.2f %05d %04d %05d %.5f\n", dif, iter,
                m_state->m_num_topics, m_state->m_total_num_tables, likelihood);
        if (m_hdp_param->m_save_lag != -1 && (iter % m_hdp_param->m_save_lag == 0))
        {
            sprintf(name, "%s/test-%05d", directory, iter);
            m_state->save_state(name);
            sprintf(name, "%s/test-%05d.bin", directory, iter);
            m_state->save_state_ex(name);
        }

        if (iter == 0 || best_likelihood < likelihood)
        {
            best_likelihood = likelihood;
            sprintf(name, "%s/test-mode", directory);
            m_state->save_state(name);
            sprintf(name, "%s/test-mode.bin", directory);
            m_state->save_state_ex(name);
        }
    }
    fclose(file);
}
