/*
  Copyright 2020 cc32d9@gmail.com

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/action.hpp>
#include <eosio/transaction.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/time.hpp>


using namespace eosio;
using std::string;
using std::vector;

CONTRACT payout : public eosio::contract {
 public:

  const uint16_t MAX_WALK = 30; // maximum walk through unapproved accounts

  payout( name self, name code, datastream<const char*> ds ):
    contract(self, code, ds)
    {}

  // sets the admin account
  ACTION setadmin(name newadmin)
  {
    req_admin();
    check(is_account(newadmin), "admin account does not exist");
    set_name_prop(name("adminacc"), newadmin);
  }

  // the account will receive the fee from every incoming deposit
  ACTION setfee(name feeacc, uint16_t permille)
  {
    req_admin();
    check(is_account(feeacc), "fee account does not exist");
    check(permille < 1000, "permille must be less than 1000");
    set_name_prop(name("feeacc"), feeacc);
    set_uint_prop(name("feepermille"), permille);
  }


  // new payout recipients need to be approved for automatic payments
  ACTION approveacc(vector<name> accounts)
  {
    req_admin();
    approvals appr(_self, 0);
    for( name& acc: accounts ) {
      auto itr = appr.find(acc.value);
      check(itr != appr.end(), "Account not found in approval list");
      if( !itr->approved ) {
        appr.modify( *itr, same_payer, [&]( auto& row ) {
                                         row.approved = 1;
                                       });
      }
    }
  }


  ACTION unapproveacc(vector<name> accounts)
  {
    req_admin();
    approvals appr(_self, 0);
    for( name& acc: accounts ) {
      auto itr = appr.find(acc.value);
      check(itr != appr.end(), "Account not found in approval list");
      if( itr->approved ) {
        appr.modify( *itr, same_payer, [&]( auto& row ) {
                                         row.approved = 0;
                                       });
      }
    }
  }


  ACTION newschedule(name payer, name schedule_name, name tkcontract, asset currency, string memo)
  {
    require_auth(payer);

    schedules sched(_self, 0);
    auto scitr = sched.find(schedule_name.value);
    check(scitr == sched.end(), "A schedule with this name already exists");
    check(is_account(tkcontract), "Token contract account does not exist");
    check(currency.is_valid(), "invalid currency" );

    // validate that such token exists
    {
      stats_table statstbl(tkcontract, currency.symbol.code().raw());
      auto statsitr = statstbl.find(currency.symbol.code().raw());
      check(statsitr != statstbl.end(), "This currency symbol does not exist");
      check(statsitr->supply.symbol.precision() == currency.symbol.precision(), "Wrong currency precision");
    }
                           
    currency.amount = 0;

    funds fnd(_self, payer.value);
    auto fndidx = fnd.get_index<name("token")>();
    auto fnditr = fndidx.find(token_index_val(tkcontract, currency));
    if( fnditr == fndidx.end() ) {
      // register a new currency in payer funds
      fnd.emplace(payer, [&]( auto& row ) {
                           row.id = fnd.available_primary_key();
                           row.tkcontract = tkcontract;
                           row.currency = currency;
                           row.dues = currency;
                           row.deposited = currency;
                         });
    }

    sched.emplace(payer, [&]( auto& row ) {
                           row.schedule_name = schedule_name;
                           row.payer = payer;
                           row.tkcontract = tkcontract;
                           row.currency = currency;
                           row.memo = memo;
                           row.dues = currency;
                           row.last_processed.value = 1; // the dues index position
                         });
  }


  // record new totals for recipients

  struct book_record {
    name  recipient;
    asset new_total;
  };

  ACTION book(name schedule_name, vector<book_record> records)
  {
    schedules sched(_self, 0);
    auto scitr = sched.find(schedule_name.value);
    check(scitr != sched.end(), "Cannot find the schedule");

    name payer = scitr->payer;
    require_auth(payer);

    asset new_dues = scitr->currency;
    approvals appr(_self, 0);

    recipients rcpts(_self, schedule_name.value);
    for( auto& rec: records ) {

      check(rec.new_total.symbol == scitr->currency.symbol, "Invalid currency symbol");
      auto rcitr = rcpts.find(rec.recipient.value);

      if( rcitr == rcpts.end() ) {
        // register a new recipient
        check(is_account(rec.recipient), "recipient account does not exist");
        new_dues += rec.new_total;
        rcpts.emplace(payer, [&]( auto& row ) {
                               row.account = rec.recipient;
                               row.booked_total = rec.new_total.amount;
                               row.paid_total = 0;
                             });

        // new recipients go to unapproved list unless approved in another schedule
        if( appr.find(rec.recipient.value) == appr.end() ) {
            appr.emplace(payer, [&]( auto& row ) {
                                  row.account = rec.recipient;
                                  row.approved = 0;
                                });
        }
      }
      else {
        // update an existing recipient
        check(rec.new_total.amount > rcitr->booked_total, "New total must be bigger than the old total");
        new_dues.amount += (rec.new_total.amount - rcitr->booked_total);
        rcpts.modify( *rcitr, same_payer, [&]( auto& row ) {
                                            row.booked_total = rec.new_total.amount;
                                          });
      }
    }

    funds fnd(_self, payer.value);
    auto fndidx = fnd.get_index<name("token")>();
    auto fnditr = fndidx.find(token_index_val(scitr->tkcontract, scitr->currency));
    check(fnditr != fndidx.end(), "This must never happen");

    check(fnditr->deposited >= fnditr->dues + new_dues, "Insufficient funds on the deposit");

    sched.modify( *scitr, same_payer, [&]( auto& row ) {
                                        row.dues += new_dues;
                                      });

    fnd.modify( *fnditr, same_payer, [&]( auto& row ) {
                                       row.dues += new_dues;
                                     });
  }


  // recipient can claim the transfer instead of waiting for automatic job (no authorization needed)
  ACTION claim(name schedule_name, name recipient)
  {
    schedules sched(_self, 0);
    auto scitr = sched.find(schedule_name.value);
    check(scitr != sched.end(), "Cannot find the schedule");

    recipients rcpts(_self, schedule_name.value);
    auto rcitr = rcpts.find(recipient.value);
    check(rcitr != rcpts.end(), "Cannot find this recipient in the schedule");
    check(rcitr->booked_total > rcitr->paid_total, "All dues are paid already");

    _pay_due(schedule_name, recipient);
  }


  // payout job that serves up to so many payments (no authorization needed)
  ACTION runpayouts(uint16_t count)
  {
    bool done_something = false;
    approvals appr(_self, 0);

    uint16_t loopcount = 0;
    bool paid_something_in_last_loop = false;
    
    while( count-- > 0 ) {
      name last_schedule = get_name_prop(name("lastschedule"));
      name old_last_schedule = last_schedule;
      if( last_schedule.value == 0 ) {
        last_schedule.value = 1;
      }

      schedules sched(_self, 0);
      auto schedidx = sched.get_index<name("dues")>();
      auto scitr = schedidx.lower_bound(last_schedule.value);
      if( scitr == schedidx.end() ) {
        // we're at the end of active schedules
        last_schedule.value = 1;
        if( loopcount > 0 && !paid_something_in_last_loop ) {
          // nothing to do more, abort the loop
          count = 0;
        }
        loopcount++;
        paid_something_in_last_loop = false;
      }
      else {
        name schedule_name = scitr->schedule_name;
        last_schedule = schedule_name;
        uint64_t last_processed = scitr->last_processed.value;
        uint64_t old_last_processed = last_processed;
        recipients rcpts(_self, schedule_name.value);
        auto rcdidx = rcpts.get_index<name("dues")>();
        auto rcitr = rcdidx.lower_bound(last_processed);

        uint16_t walk_limit = MAX_WALK;
        bool paid = false;
        while( !paid && walk_limit-- > 0 ) {
          if( rcitr == rcdidx.end() ) {
            // end of recpients list, abort the loop
            walk_limit = 0;
            last_processed = 1;
          }
          else {
            last_processed = rcitr->account.value;
            auto apritr = appr.find(rcitr->account.value);
            check(apritr != appr.end(), "This should never happen");
            if( apritr->approved ) {
              _pay_due(schedule_name, rcitr->account);
              paid = true;
              paid_something_in_last_loop = true;
            }
            else {
              // this is an unapproved account, skip to the next
              rcitr++;
            }
          }
        }

        if( last_processed != old_last_processed ) {
          sched.modify( *scitr, same_payer, [&]( auto& row ) {
                                              row.last_processed.value = last_processed;
                                            });
          done_something = true;
        }
      }

      if( last_schedule != old_last_schedule ) {
        set_name_prop(name("lastschedule"), last_schedule);
        done_something = true;
      }
    }

    check(done_something, "Nothing to do");
  }

  [[eosio::on_notify("*::transfer")]] void on_payment (name from, name to, asset quantity, string memo) {
    if(to == _self) {
      funds fnd(_self, from.value);
      auto fndidx = fnd.get_index<name("token")>();
      auto fnditr = fndidx.find(token_index_val(name{get_first_receiver()}, quantity));
      check(fnditr != fndidx.end(), "There are no schedules with this currency for this payer");
      check(quantity.amount > 0, "expected a positive amount");

      uint64_t feepermille = get_uint_prop(name("feepermille"));
      name feeacc = get_name_prop(name("feeacc"));

      if( feepermille > 0 && feeacc != name("") ) {
        asset fee = quantity * feepermille / 1000;
        quantity -= fee;
        extended_asset xfee(fee, fnditr->tkcontract);
        send_payment(feeacc, xfee, "fees");
      }

      fnd.modify( *fnditr, same_payer, [&]( auto& row ) {
                                         row.deposited += quantity;
                                       });
    }
  }

  // for testing purposes only!
  ACTION wipeall(uint16_t count)
  {
    require_auth(_self);

    schedules sched(_self, 0);
    auto scitr = sched.begin();

    while( scitr != sched.end() && count-- > 0 ) {
      
      funds fnd(_self, scitr->payer.value);
      auto fnditr = fnd.begin();
      while( fnditr != fnd.end() ) {
        fnditr = fnd.erase(fnditr);
      }

      recipients rcpts(_self, scitr->schedule_name.value);
      auto rcitr = rcpts.begin();
      while( rcitr != rcpts.end() ) {
        rcitr = rcpts.erase(rcitr);
      }

      scitr = sched.erase(scitr);
    }

    {
      approvals appr(_self, 0);
      auto itr = appr.begin();
      while( itr != appr.end() ) {
        itr = appr.erase(itr);
      }
    }
  }
    

 private:

  // properties table for keeping contract settings

  struct [[eosio::table("props")]] prop {
    name           key;
    name           val_name;
    uint64_t       val_uint = 0;
    auto primary_key()const { return key.value; }
  };

  typedef eosio::multi_index<
    name("props"), prop> props;

  void set_name_prop(name key, name value)
  {
    props p(_self, 0);
    auto itr = p.find(key.value);
    if( itr != p.end() ) {
      p.modify( *itr, same_payer, [&]( auto& row ) {
                                    row.val_name = value;
                                  });
    }
    else {
      p.emplace(_self, [&]( auto& row ) {
                         row.key = key;
                         row.val_name = value;
                       });
    }
  }

  name get_name_prop(name key)
  {
    props p(_self, 0);
    auto itr = p.find(key.value);
    if( itr != p.end() ) {
      return itr->val_name;
    }
    else {
      p.emplace(_self, [&]( auto& row ) {
                         row.key = key;
                       });
      return name();
    }
  }


  void set_uint_prop(name key, uint64_t value)
  {
    props p(_self, 0);
    auto itr = p.find(key.value);
    if( itr != p.end() ) {
      p.modify( *itr, same_payer, [&]( auto& row ) {
                                    row.val_uint = value;
                                  });
    }
    else {
      p.emplace(_self, [&]( auto& row ) {
                         row.key = key;
                         row.val_uint = value;
                       });
    }
  }

  uint64_t get_uint_prop(name key)
  {
    props p(_self, 0);
    auto itr = p.find(key.value);
    if( itr != p.end() ) {
      return itr->val_uint;
    }
    else {
      p.emplace(_self, [&]( auto& row ) {
                         row.key = key;
                       });
      return 0;
    }
  }

  // table recipients approval

  struct [[eosio::table("approvals")]] approval {
    name           account;
    uint8_t        approved;
    
    auto primary_key()const { return account.value; }
    uint64_t by_approved()const { return approved ? account.value:0; }
    uint64_t by_unapproved()const { return (!approved) ? account.value:0; }
  };

  typedef eosio::multi_index<
    name("approvals"), approval,
    indexed_by<name("approved"), const_mem_fun<approval, uint64_t, &approval::by_approved>>,
    indexed_by<name("unapproved"), const_mem_fun<approval, uint64_t, &approval::by_unapproved>>
    > approvals;



  // all registered schedules
  struct [[eosio::table("schedules")]] schedule {
    name           schedule_name;
    name           payer;
    name           tkcontract;
    asset          currency;
    string         memo;
    asset          dues;  // how much we need to send to recipients
    name           last_processed; // last paid recipient

    auto primary_key()const { return schedule_name.value; }
    // index for iterating through active schedules
    uint64_t by_dues()const { return (dues.amount > 0) ? payer.value:0; }
  };

  typedef eosio::multi_index<
    name("schedules"), schedule,
    indexed_by<name("dues"), const_mem_fun<schedule, uint64_t, &schedule::by_dues>>
    > schedules;


  static inline uint128_t token_index_val(name tkcontract, asset currency)
  {
    return ((uint128_t)tkcontract.value << 64)|(uint128_t)currency.symbol.raw();
  }

  // assets belonging to the payer. scope=payer
  struct [[eosio::table("funds")]] fundsrow {
    uint64_t       id;
    name           tkcontract;
    asset          currency;
    asset          dues;  // how much we need to send to recipients
    asset          deposited; // how much we can spend

    auto primary_key()const { return id; }
    uint128_t by_token()const { return token_index_val(tkcontract, currency); }
  };

  typedef eosio::multi_index<
    name("funds"), fundsrow,
    indexed_by<name("token"), const_mem_fun<fundsrow, uint128_t, &fundsrow::by_token>>
    > funds;


  // payment recipients, scope=scheme_name
  struct [[eosio::table("recipients")]] recipient {
    name           account;
    uint64_t       booked_total; // asset amount
    uint64_t       paid_total;

    auto primary_key()const { return account.value; }
    // index for iterating through open dues
    uint64_t by_dues()const { return (booked_total > paid_total) ? account.value:0; }
  };

  typedef eosio::multi_index<
    name("recipients"), recipient,
    indexed_by<name("dues"), const_mem_fun<recipient, uint64_t, &recipient::by_dues>>
    > recipients;


  void req_admin()
  {
    name admin = get_name_prop(name("adminacc"));
    if( admin == name("") ) {
      require_auth(_self);
    }
    else {
      require_auth(admin);
    }
  }


  struct transfer_args
  {
    name         from;
    name         to;
    asset        quantity;
    string       memo;
  };

  void send_payment(name recipient, const extended_asset& x, const string memo)
  {
    action
      {
        permission_level{_self, name("active")},
          x.contract,
            name("transfer"),
            transfer_args  {
            .from=_self, .to=recipient,
                              .quantity=x.quantity, .memo=memo
                              }
      }.send();
  }


  // does not check any validity, just performs a transfer and bookeeping
  void _pay_due(name schedule_name, name recipient) {
    schedules sched(_self, 0);
    auto scitr = sched.find(schedule_name.value);

    funds fnd(_self, scitr->payer.value);
    auto fndidx = fnd.get_index<name("token")>();
    auto fnditr = fndidx.find(token_index_val(scitr->tkcontract, scitr->currency));
    check(fnditr != fndidx.end(), "This must never happen");

    recipients rcpts(_self, schedule_name.value);
    auto rcitr = rcpts.find(recipient.value);

    asset due(rcitr->booked_total - rcitr->paid_total, scitr->currency.symbol);
    extended_asset pay(due, scitr->tkcontract);
    send_payment(recipient, pay, scitr->memo);

    rcpts.modify( *rcitr, same_payer, [&]( auto& row ) {
                                        row.paid_total = row.booked_total;
                                      });

    sched.modify( *scitr, same_payer, [&]( auto& row ) {
                                        row.dues -= due;
                                      });

    fnd.modify( *fnditr, same_payer, [&]( auto& row ) {
                                       row.dues -= due;
                                       row.deposited -= due;
                                     });
  }

  // eosio.token structure
  struct currency_stats {
    asset  supply;
    asset  max_supply;
    name issuer;

    uint64_t primary_key()const { return supply.symbol.code().raw(); }
  };

  typedef eosio::multi_index<"stat"_n, currency_stats> stats_table;
};
