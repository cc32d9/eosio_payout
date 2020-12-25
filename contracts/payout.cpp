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

CONTRACT payout : public eosio::contract {
 public:
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

    approved_tbl appr(_self, 0);
    unapproved_tbl unappr(_self, 0);

    for( name& acc: accounts ) {
      auto itr = unappr.find(acc);
      check(itr != unappr.end(), "Account not found in unapproved list");
      appr.emplace(_self, [&]( auto& row ) {
                            row.account = acc;
                          });
      unappr.erase(itr);
    }
  }


  ACTION unapproveacc(vector<name> accounts)
  {
    req_admin();

    approved_tbl appr(_self, 0);
    unapproved_tbl unappr(_self, 0);

    for( name& acc: accounts ) {
      auto itr = appr.find(acc);
      check(itr != appr.end(), "Account not found in approved list");
      unappr.emplace(_self, [&]( auto& row ) {
                              row.account = acc;
                            });
      appr.erase(itr);
    }
  }


  ACTION newschedule(name payer, name tkcontract, asset currency, string memo)
  {
    requre_auth(payer);

    schedules sched(_self, 0);
    auto scitr = sched.find(payer);
    check(scitr == sched.end(), "A schedule for this payer already exists");
    check(is_account(tkcontract), "Token contract account does not exist");
    check(currency.is_valid(), "invalid currency" );

    currency.amount = 0;

    sched.emplace(payer, [&]( auto& row ) {
                           row.payer = payer;
                           row.tkcontract = tkcontract;
                           row.currency = currency;
                           row.memo = memo;
                           row.dues = currency;
                           row.deposited = currency;
                         });
  }


  // record new totals for recipients

  struct book_record {
    name  recipient;
    asset new_total;
  }

  ACTION book(name payer, vector<book_record> records)
  {
    requre_auth(payer);
    schedules sched(_self, 0);
    auto scitr = sched.find(from);
    check(scitr != sched.end(), "Cannot find the schedule");

    asset new_dues = scitr->currency;

    approved_tbl appr(_self, 0);
    unapproved_tbl unappr(_self, 0);

    recipients rcpts(_self, payer.value);
    for( auto& rec: records ) {

      check(rec.new_total.symbol == scitr->currency.symbol, "Invalid currency symbol");
      auto rcitr = rcpts.find(rec.recipient);

      if( rcitr == rcpts.end() ) {
        // register a new recipient
        new_dues += rec.new_total;
        rcpts.emplace(payer, [&]( auto& row ) {
                               row.account = rec.recipient;
                               row.booked_total = rec.new_total.amount;
                               row.paid_total = 0;
                             });

        // new recipients go to unapproved list unless approved in another schedule
        if( appr.find(rec.recipient) == appr.end() &&
            unappr.find(rec.recipient) == unappr.end() ) {
          unappr.emplace(payer, [&]( auto& row ) {
                                  row.account = rec.recipient;
                                });
        }
      }
      else {
        // update an existing recipient
        check(rec.new_total > rcitr->booked_total, "New total must be bigger than the old total");
        new_dues += rec.new_total - rcitr->booked_total;
        rcpts.modify( *rcitr, same_payer, [&]( auto& row ) {
                                            row.booked_total = rec.new_total.amount;
                                          });
      }
    }

    check(scitr->deposited >= scitr->dues + new_dues, "Insufficient funds on the deposit");
    sched.modify( *scitr, same_payer, [&]( auto& row ) {
                                        row.dues += new_dues;
                                      });
  }


  // recipient can claim the transfer instead of waiting for automatic job
  ACTION claim(name payer, name recipient)
  {
    requre_auth(payer);

  }

  // payout job that serves up to so many payments (no authorization needed)
  ACTION runpayouts(uint16_t count)
  {
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
    auto itr = p.find(key);
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
    auto itr = p.find(key);
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
    auto itr = p.find(key);
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
    auto itr = p.find(key);
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

  // tables for approved and unapproved recipients

  struct [[eosio::table("approved")]] approved {
    name           account;
    auto primary_key()const { return account.value; }
  };

  typedef eosio::multi_index<
    name("approved"), approved> approved_tbl;

  struct [[eosio::table("unapproved")]] unapproved {
    name           account;
    auto primary_key()const { return account.value; }
  };

  typedef eosio::multi_index<
    name("unapproved"), unapproved> unapproved_tbl;


  // all registered schedules
  struct [[eosio::table("schedules")]] schedule {
    name           payer;
    name           tkcontract;
    asset          currency;
    string         memo;
    asset          dues;  // how much we need to send to recipients
    asset          deposited; // how much we can spend
    name           last_processed; // last paid recipient

    auto primary_key()const { return payer.value; }
    // index for iterating through active schedules
    uint64_t by_dues()const { return (dues.amount > 0) ? payer.value:0; }
  };

  typedef eosio::multi_index<
    indexed_by<name("dues"), const_mem_fun<schedule, uint64_t, &schedule::by_dues>>
    name("schedules"), schedule> schedules;


  // payment recipients, scope=payer
  struct [[eosio::table("recipients")]] recipient {
    name           account;
    uint64_t       booked_total; // asset amount
    uint64_t       paid_total;

    auto primary_key()const { return account.value; }
    // index for iterating through open dues
    uint64_t by_dues()const { return (booked_total > paid_total) ? account.value:0; }
  };

  typedef eosio::multi_index<
    indexed_by<name("dues"), const_mem_fun<recipient, uint64_t, &recipient::by_dues>>
    name("recipients"), recipient> recipients;


  void req_admin()
  {
    name admin = get_name_prop(name("adminacc"));
    if( admin == name("") ) {
      requre_auth(_self);
    }
    else {
      requre_auth(admin);
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

  [[eosio::on_notify("*::transfer")]] void on_handler (name from, name to, asset quantity, string memo) {
    if(to == _self) {
      schedules sched(_self, 0);
      auto scitr = sched.find(from);
      check(scitr != sched.end(), "Only payments from registered payers allowed");
      check(name{get_first_receiver()} == scitr->tkcontract, "invalid token contract");
      check(quantity.symbol == scitr->currency.symbol, "invalid currency symbol");
      check(quantity.amount > 0, "expected a positive amount");

      uint64_t feepermille = get_uint_prop("feepermille");
      name feeacc = get_name_prop(name("feeacc"));

      if( feepermille > 0 && feeacc != name("") ) {
        asset fee = quantity * feepermille / 1000;
        quantity -= fee;
        extended_asset xfee(fee, scitr->tkcontract);
        send_payment(feeacc, xfee, scitr->memo);
      }

      sched.modify( *scitr, same_payer, [&]( auto& row ) {
                                          row.deposited += quantity;
                                        });
    }
  }

  // does not check any validity, just performs a transfer and bookeeping
  void _pay_due(name payer, name recipient) {
    schedules sched(_self, 0);
    auto scitr = sched.find(payer);

    recipients rcpts(_self, payer.value);
    auto rcitr = rcpts.find(recipient);

    asset due(rcitr->booked_total - rcitr->paid_total, scitr->currency.symbol);
    extended_asset pay(due, scitr->tkcontract);
    send_payment(recipient, pay, scitr->memo);

    rcpts.modify( *rcitr, same_payer, [&]( auto& row ) {
                                        row.paid_total = row.booked_total;
                                      });
    
    sched.modify( *scitr, same_payer, [&]( auto& row ) {
                                        row.dues -= pay;
                                      });
  }
  
};
