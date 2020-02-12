#pragma once

// return epoch that the transaction exists in
unsigned begin_tx(void);

// end the current transaction
void end_tx(unsigned c);

// advance the epoch and persist any remaining transactions
// that happened > 1 epoch ago
void advance_epoch(unsigned e);

// help functions to assist in the 
void help_free(unsigned n);
void help_persist_old(unsigned n);
void help_persist_new(unsigned n);

// initialize shared memory
void init_persistence();
