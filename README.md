# EOSIO Payout Engine

Anyone who built automated payments on an EOSIO blockchain knows the
problem well: the payments may fail for a number of reasons, such as
insufficient CPU or RAM on sender account, or the transaction gets
dropped by a microfork.

The Payout Engine solves this problem. Whenever there's a failure, it
guarantees to retry and never double-spend.

The engine is available for any project that needs to automate
payments, such as e-commerce, dividends, salary, etc.

The engine consists of the following components:

* The payout smart contract that registers the bookings and keeps
  track of outgoing payments;

* The server-side daemon does a number of background jobs: it verifies
  if a recipient runs a smart contract and blocks the transfers;
  checks if new payments are due, and executes the payouts;

* The payment management script on the payer side: it compares some
  internal database with booked payments, and updates the smart
  contract when necessary.


## Roles and definitions

The following roles are defined for blockchain accounts:

* admin: it can approve or disapprove recipient accounts. This is
  needed in order to avoid paying to smart contracts which may reject
  the payment or abuse our CPU resource.

* fees collector: whenever the payer transfers a deposit to the payout
  contract, 0.5% of the deposit amount is sent automatically to the
  fee collection account. This money is used to support the hosting
  and development of the engine.

* payer: whenever wants to distribute tokens through the engine, is
  registering themselves as a payer account with one or several
  schedules.

* recipient: the account that needs to receive payments from the
  payer. The system allows any number of recipients.


A *schedule* is an object that defines a relationship between a payer
and recipients. It has the following attributes:

* Payer account: this account is allowed to deposit funds into the
  contract and book the outgoing payments.

* Schedule name: a string of up to 12 symbols, consisting of letters
  a-z, numbers 1-5 and dots. The only constraint is that the names
  should be unique.

* Token contract and currency: all payments within a schedule are made
  in one token that is defined when the schedule is created.

* Memo string: this string will be used as transfer memo in outgoing
  payments. This string cannot be changed after the schedule is
  created.


## Engine workflow

1. Payer creates a schedule by executing the `newschedule` action,
specifying the name, currency, and memo string. A payer can create
multiple schedules in different or the same currency.

2. Payer places a deposit in the same currency as specified in the
schedule. The contract deducts automatically 0.5% fee from it.

3. Payer books the outgoing payments by calling the `book` action. The
action takes the schedule name and a list of (recipient, newtotal)
tuples. Recipient should be a valid blockchain account, and newtotal
is the total amount of tokens that the receiver should get in the
whole history. So, it's an always growing number for the same
recipient. The booking cannot exceed the deposited funds. If the payer
needs to book more, a new deposit is required.

4. The admin process checks if the new recipients run any smart
contracts, and approves them if they don't run any. The ones
unapproved can claim the payments when needed.

5. The admin process checks all due amounts and sends out payments to
the recipients. The smart contract registers all amounts it sends, so
only the difference between newtotal and total paid amount is sent
out.

Whenever there's a transaction failure, the payer will book again with
the same or updated newtotal. Also the admin script will retry if its
transaction fails.

The automatic payouts are performed in round-robin sequence: one due
payment from a schedule is sent, then the next schedule is
selected. This guarantees that a massive schedule does not block the
less frequent ones.

The payer's RAM quota is charged for creating all memory structures
related to the schedule. Currently the contract allocates the RAM for
recipient token balances if the recipient didn't have such token, but
it will change in the future, and the payer will be charged for such
expenses.


## Production deployment

The account `payoutengine` is deployed on the following EOSIO blockchains:

* EOS Mainnet

* Telos Mainnet

* WAX Mainnet

* Jungle Testnet

* Telos Testnet

* WAX Testnet


For the time being, the contract is managed by `cc32dninexxx`
account. Later on, the management will be transferred to a multisig
among well-known organizations.


## Smart contract actions and tables

Only actions and tables for user calling are listed below.


### action: `newschedule`

The action is called once to initiate a schedule. The payer is charged for RAM.

```
ACTION newschedule(name payer, name schedule_name, name tkcontract, asset currency, string memo)
```

* `payer`: the account that creates a new schedule, and this account will be a designated payer for it.

* `schedule_name`: a unique name for the schedule. Letters `a-z`,
  numbers `1-5` and dots (`.`) are allowed, up to 12 characters.

* `tkcontract`: token contract (e.g. `eosio.token` for the system
  token).

* `currency`: token symbol and precision (e.g. `0.0000 EOS`).

* `memo`: a string up to 256 characters long that will be used as
  default memo string in outbound token transfers.


### transfer handler

Normal token transfers are accepted by contract. it only accepts them
from payers which have registered schedules, and only in the token
currency that is specified in a schedule. The deposited token can be
spent by calling `book` or `bookm` actions.


### action: `book`

The action books new due amounts toward recipients for a specific
schedule. Only the payer registered with the schedule is allowed to
execute it. At least one amount should be higher than a previously
booked total for a given recipient. It is recommended to look up
`booked_total` in `recipients` table for a particular recipient, and
only send the `book` action if the new due amount is higher than
`booked_total`.

```
  struct book_record {
    name  recipient;
    asset new_total;
  };

  ACTION book(name schedule_name, vector<book_record> records)
```

* `schedule_name`: a schedule name previously registered with
  `newschedule`.

* `records`: a vector of structures with the following fields:

  * `recipient`: a valid EOSIO account that will receive the payment;

  * `new_total`: total amount of token that the recipient should
    receive throughout its lifetime (so this amount should only grow
    with each action call).


### action: `bookm`

The action is similar to `book`, but the structures have an additional
field, `memo`, which specifies the message that will be used in
outgoing transfer for this recipient.

```
  struct bookm_record {
    name  recipient;
    asset new_total;
    string memo;
  };

  ACTION bookm(name schedule_name, vector<bookm_record> records)
```


### action: `claim`

Accounts that have smart contracts are by default blocked from
automatic outgoing payments. Such recipients can initiate the transfer
by calling the `claim` action. There is no authorization, so any
account can call it to start the transfer for another recipient.

Recipients can also call this to speed up the outgoing transfers if
the automatic dispatcher is too busy.

```
  ACTION claim(name schedule_name, name recipient)
```

* `schedule_name`: a valid schedule name;

* `recipient`: the recipient account that will receive a payment if
  there is an outstanding due amount,


### table: `recipients`

This table is the only one that payer needs to look up in order to
optimize the bookings. The scope is set to the schedule name, and
recipient name is the primary index. `booked_total` is an integer, so
it's the currency amount multiplied by a power of 10 corresponding to
the token precision.

Once the payer recognizes that `booked_total` is lower than the total
due amount for a recipient, it's time to execute the `book` action.

```
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
```

### table: `funds`

This table is useful for monitoring the remaining token balance for a
payer. Scope is set to the payer account, and the primary key is a
running integer.

`deposited` indicates the available funds. It's decreased every time
an outgoing payment is made from corresponding payer and in
corresponding currency.

`dues` indicates the amount that is booked to be sent out to
recipients.

```
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
```




## Copyright and License

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


## Donations and paid service

ETH address: `0x7137bfe007B15F05d3BF7819d28419EAFCD6501E`

EOS account: `cc32dninexxx`
