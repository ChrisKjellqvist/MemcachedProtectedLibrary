#include "EpochSys.hpp"
namespace pds{

__thread int _tid = -1;
EpochSys* esys = nullptr;
padded<uint64_t>* epochs = nullptr;
SysMode sys_mode = ONLINE;

UIDGenerator PBlk::uid_generator;

void EpochSys::parse_env(GlobalTestConfig* gtc){
    if (to_be_persisted){
        delete to_be_persisted;
    }
    if (to_be_freed){
        delete to_be_freed;
    }
    if (epoch_advancer){
        delete epoch_advancer;
    }
    if (trans_counter){
        delete trans_counter;
    }

    if (gtc->checkEnv("Persist")){
        if (gtc->getEnv("Persist") == "No"){
            to_be_persisted = new NoToBePersistContainer();
            to_be_freed = new NoToBeFreedContainer();
            epoch_advancer = new NoEpochAdvancer();
            trans_counter = new NoTransactionCounter(this->global_epoch);
            return;
        }
    }

    if (gtc->checkEnv("Persist")){
        string env_persist = gtc->getEnv("Persist");
        if (env_persist == "DirWB"){
            to_be_persisted = new DirWBToBePersistContainer();
        } else if (env_persist == "PerLine"){
            to_be_persisted = new PerLineToBePersistContainer();
        } else if (env_persist == "DelayedWB"){
            to_be_persisted = new DelayedWBToBePersistContainer(task_num);
        } else if (env_persist == "BufferedWB"){
            to_be_persisted = new BufferedWBToBePersistContainer(task_num);
        } else {
            errexit("unrecognized persist environment");
        }
    } else {
        gtc->setEnv("Persist", "DelayedWB");
        to_be_persisted = new DelayedWBToBePersistContainer(task_num);
    }

    if (gtc->checkEnv("Free")){
        string env_free = gtc->getEnv("Free");
        if (env_free == "Yes"){
            to_be_freed = new ToBeFreedContainer();
        } else if (env_free == "No"){
            to_be_freed = new NoToBeFreedContainer();
        } else {
            errexit("unrecognized free environment");
        }
    } else {
        to_be_freed = new ToBeFreedContainer();
    }

    if (gtc->checkEnv("Container")){
        string env_container = gtc->getEnv("Container");
        if (env_container == "CircBuffer"){
            to_be_persisted->attach_container(new CircBufferContainer<PBlk*>(task_num));
            to_be_freed->attach_container(new CircBufferContainer<PBlk*>(task_num));
        } else if (env_container == "Vector"){
            to_be_persisted->attach_container(new VectorContainer<PBlk*>(task_num));
            to_be_freed->attach_container(new VectorContainer<PBlk*>(task_num));
        } else {
            errexit("unrecognized container environment");
        }
    } else {
        gtc->setEnv("Container", "Vector");
        to_be_persisted->attach_container(new VectorContainer<PBlk*>(task_num));
        to_be_freed->attach_container(new VectorContainer<PBlk*>(task_num));
    }

    if (gtc->checkEnv("TransCounter")){
        string env_transcounter = gtc->getEnv("TransCounter");
        if (env_transcounter == "Atomic"){
            trans_counter = new AtomicTransactionCounter(this->global_epoch);
        } else if (env_transcounter == "NoFence"){
            trans_counter = new NoFenceTransactionCounter(this->global_epoch, task_num);
        } else if (env_transcounter == "FenceBegin"){
            trans_counter = new FenceBeginTransactionCounter(this->global_epoch, task_num);
        } else if (env_transcounter == "FenceEnd"){
            trans_counter = new FenceEndTransactionCounter(this->global_epoch, task_num);
        } else if (env_transcounter == "PerEpoch"){
            trans_counter = new PerEpochTransactionCounter(this->global_epoch, task_num);
        } else {
            errexit("unrecognized transaction counter environment");
        }
    } else {
        gtc->setEnv("TransCounter", "PerEpoch");
        trans_counter = new PerEpochTransactionCounter(this->global_epoch, task_num);
    }
    
    if (gtc->checkEnv("EpochAdvance")){
        string env_epochadvance = gtc->getEnv("EpochAdvance");
        if (env_epochadvance == "Global"){
            epoch_advancer = new GlobalCounterEpochAdvancer();
        } else if (env_epochadvance == "SingleThread"){
            epoch_advancer = new SingleThreadEpochAdvancer(gtc);
        } else if (env_epochadvance == "Dedicated"){
            epoch_advancer = new DedicatedEpochAdvancer(gtc, this);
        } else {
            errexit("unrecognized epoch advance argument");
        }
    } else {
        gtc->setEnv("EpochAdvance", "Dedicated");
        epoch_advancer = new DedicatedEpochAdvancer(gtc, this);
    }
    
    if (gtc->checkEnv("EpochFreq")){
        int env_epoch_advance = stoi(gtc->getEnv("EpochFreq"));
        if (gtc->getEnv("EpochAdvance") != "Dedicated" && env_epoch_advance > 63){
            errexit("invalid EpochFreq power");
        }
        epoch_advancer->set_epoch_freq(env_epoch_advance);
    }
    if (gtc->checkEnv("HelpFreq")){
        int env_help_freq = stoi(gtc->getEnv("HelpFreq"));
        if (gtc->getEnv("EpochAdvance") != "Dedicated" && env_help_freq > 63){
            errexit("invalid HelpFreq power");
        }
        epoch_advancer->set_help_freq(env_help_freq);
    }
}

uint64_t EpochSys::begin_transaction(){
    uint64_t ret;
    do{
        ret = global_epoch->load(std::memory_order_seq_cst);
    } while(!trans_counter->consistent_register_active(ret, ret));
    return ret;
}

void EpochSys::end_transaction(uint64_t c){
    trans_counter->unregister_active(c);
    epoch_advancer->on_end_transaction(this, c);
}

void EpochSys::end_readonly_transaction(uint64_t c){
    trans_counter->unregister_active(c);
}

// the same as end_readonly_transaction, but semantically different. Repeat to avoid confusion.
void EpochSys::abort_transaction(uint64_t c){
    trans_counter->unregister_active(c);
}

void EpochSys::validate_access(const PBlk* b, uint64_t c){
    if (c == NULL_EPOCH){
        errexit("access with NULL_EPOCH. BEGIN_OP not called?");
    }
    if (b->epoch > c){
        throw OldSeeNewException();
    }
}

void EpochSys::register_update_pblk(PBlk* b, uint64_t c){
    // to_be_persisted[c%4].push(b);
    if (c == NULL_EPOCH){
        // update before BEGIN_OP, return. This register will be done by BEGIN_OP.
        return;
    }
    to_be_persisted->register_persist(b, c);
}

// Arg is epoch we think we're ending
void EpochSys::advance_epoch(uint64_t c){
    // TODO: if we go with one bookkeeping thread, remove unecessary synchronizations.

    // Free all retired blocks from 2 epochs ago
    if (!trans_counter->consistent_register_bookkeeping(c-2, c)){
        return;
    }
    
    to_be_freed->help_free(c-2);

    trans_counter->unregister_bookkeeping(c-2);

    // Wait until any other threads freeing such blocks are done
    while(!trans_counter->no_bookkeeping(c-2)){
        if (global_epoch->load(std::memory_order_acquire) != c){
            return;
        }
    }

    // Wait until all threads active one epoch ago are done
    if (!trans_counter->consistent_register_bookkeeping(c-1, c)){
        return;
    }
    while(!trans_counter->no_active(c-1)){
        if (global_epoch->load(std::memory_order_acquire) != c){
            return;
        }
    }

    // Persist all modified blocks from 1 epoch ago
    // while(to_be_persisted->persist_epoch(c-1));
    to_be_persisted->persist_epoch(c-1);

    trans_counter->unregister_bookkeeping(c-1);

    // Wait until any other threads persisting such blocks are done
    while(!trans_counter->no_bookkeeping(c-1)){
        if (global_epoch->load(std::memory_order_acquire) != c){
            return;
        }
    }
    // persist_func::sfence(); // given the length of current epoch, we may not need this.
    // Actually advance the epoch
    global_epoch->compare_exchange_strong(c, c+1, std::memory_order_seq_cst);
    // Failure is harmless
}

// TODO: put epoch advancing logic into epoch advancers.
void EpochSys::advance_epoch_dedicated(uint64_t c){
    // Free all retired blocks from 2 epochs ago
    to_be_freed->help_free(c-2);
    // Wait until all threads active one epoch ago are done
    while(!trans_counter->no_active(c-1)){}
    // Persist all modified blocks from 1 epoch ago
    to_be_persisted->persist_epoch(c-1);
    // persist_func::sfence(); // given the length of current epoch, we may not need this.
    // Actually advance the epoch
    global_epoch->compare_exchange_strong(c, c+1, std::memory_order_seq_cst);
    // Failure is harmless
}

// TODO: figure out how/whether to do helping with existence of dedicated bookkeeping thread(s)
void EpochSys::help_local(){
    // // Free retired blocks from 2 epochs ago
    // uint64_t c;
    // do{
    //     c = global_epoch->load(std::memory_order_acquire);
    // }while(!trans_counter->consistent_register_bookkeeping(c-2, c));
    
    // while(to_be_freed->help_free_local(c-2));
    // trans_counter->unregister_bookkeeping(c-2);
}

void EpochSys::help(){
    // // Free retired blocks from 2 epochs ago
    // uint64_t c;
    // do{
    //     c = global_epoch->load(std::memory_order_acquire);
    // }while(!trans_counter->consistent_register_bookkeeping(c-2, c));
    
    // while(to_be_freed->help_free(c-2));
    // trans_counter->unregister_bookkeeping(c-2);

    // // Persist all modified blocks from 1 epoch ago
    // do{
    //     c = global_epoch->load(std::memory_order_acquire);
    // }while(!trans_counter->consistent_register_bookkeeping(c-1, c));

    // while(to_be_persisted->persist_epoch(c-1));
    // // persist_func::sfence(); // this might not be needed. (#1)
    // trans_counter->unregister_bookkeeping(c-1);

    // // Persist modified blocks from current epoch
    // do{
    //     c = global_epoch->load(std::memory_order_acquire);
    // }while(!trans_counter->consistent_register_bookkeeping(c-1, c));

    // while(to_be_persisted->persist_epoch(c));
    // // persist_func::sfence(); // this might not be needed. (#1)
    // trans_counter->unregister_bookkeeping(c);
}

std::unordered_map<uint64_t, PBlk*>* EpochSys::recover(){
    bool clean_start;
    auto itr_raw = Persistent::recover();

    // set system mode to ONLINE -- all PDELETE_DATA becomes no-ops.

    // make a whole pass thorugh all blocks, find the epoch block.
    epoch_container = nullptr;
    if(itr_raw.is_dirty()) {
        clean_start = false;
        // dirty restart, epoch system and app need to handle
    } else {
        clean_start = true;
        // clean restart, epoch system and app may still need iter to do something
    }
    for(; !itr_raw.is_last(); ++itr_raw) { // iter++ is temporarily not supported
        PBlk* curr_blk = (PBlk*)*itr_raw;
        // use curr_blk to do higher level recovery
        if (curr_blk->blktype == EPOCH){
            epoch_container = (Epoch*) curr_blk;
            global_epoch = &epoch_container->global_epoch;
            // we continue this pass to the end to help ralloc recover.
        }
    }
    if (!epoch_container){
        errexit("epoch container not found during recovery.");
    }
    
    // make a second pass through all blocks, compute a set of in-use blocks and return the others (to ralloc).
    uint64_t epoch_cap = global_epoch->load(std::memory_order_relaxed) - 2;
    std::unordered_set<PBlk*> not_in_use;
    std::unordered_set<uint64_t> delete_nodes;
    std::unordered_map<uint64_t, PBlk*>* in_use;
    std::unordered_multimap<uint64_t, PBlk*> owned;
    in_use = new std::unordered_map<uint64_t, PBlk*>();
    new (&itr_raw) auto(Persistent::recover());
    for(; !itr_raw.is_last(); ++itr_raw) { // iter++ is temporarily not supported
        PBlk* curr_blk = (PBlk*)*itr_raw;
        // use curr_blk to do higher level recovery
        if (curr_blk == NULL_EPOCH || curr_blk->epoch > epoch_cap){
            not_in_use.insert(curr_blk);
        } else {
            switch(curr_blk->blktype){
            case OWNED:
                owned.insert(std::pair<uint64_t, PBlk*>(curr_blk->owner_id, curr_blk));
            break;
            case ALLOC:
                if (in_use->insert({curr_blk->id, curr_blk}).second == false){
                    if (clean_start){
                        errexit("more than one record with the same id after a clean exit.");
                    }
                    not_in_use.insert(curr_blk);
                }
            break;
            case UPDATE:{
                auto search = in_use->find(curr_blk->id);
                if (search != in_use->end()){
                    if (clean_start){
                        errexit("more than one record with the same id after a clean exit.");
                    }
                    if (curr_blk->epoch > search->second->epoch){
                        not_in_use.insert(search->second);
                        search->second = curr_blk; // TODO: double-check if this is right.
                    } else {
                        not_in_use.insert(curr_blk);
                    }
                } else {
                    in_use->insert({curr_blk->id, curr_blk});
                }
            }
            break;
            case DELETE:
                if (clean_start){
                    errexit("delete node appears after a clean exit.");
                }
                delete_nodes.insert(curr_blk->id);
                not_in_use.insert(curr_blk);
            break;
            case EPOCH:
            break;
            default:
                errexit("wrong type of pblk discovered");
            break;
            }
        }
    }

    if (clean_start){
        return in_use;
    }

    // make a pass through in-use blocks and owned blocks, remove those marked by delete_nodes:
    for (auto itr = delete_nodes.begin(); itr != delete_nodes.end(); itr++){
        // remove deleted in-use blocks
        auto deleted = in_use->extract(*itr);
        if (!deleted.empty()){
            not_in_use.insert(deleted.mapped());
        }
        // remove deleted owned blocks
        auto owned_blks = owned.equal_range(*itr);
        for (auto owned_itr = owned_blks.first; owned_itr != owned_blks.second; owned_itr++){
            not_in_use.insert(owned_itr->second);
        }
        owned.erase(*itr);
    }

    // make a pass through owned blocks, remove orphaned blocks:
    std::unordered_set<uint64_t> orphaned;
    for (auto itr = owned.begin(); itr != owned.end(); itr++){
        if (in_use->find(itr->first) == in_use->end()){
            orphaned.insert(itr->first);
        }
    }
    for (auto itr = orphaned.begin(); itr != orphaned.end(); itr++){
        auto orphaned_blks = owned.equal_range(*itr);
        for (auto orphaned_itr = orphaned_blks.first; orphaned_itr != orphaned_blks.second; orphaned_itr++){
            not_in_use.insert(orphaned_itr->second);
        }
        owned.erase(*itr); // we don't actually need to do this.
    }

    // reclaim all nodes in not_in_use bag
    for (auto itr = not_in_use.begin(); itr != not_in_use.end(); itr++){
        delete(*itr);
    }

    // set system mode back to online
    sys_mode = ONLINE;

    return in_use;
}


}