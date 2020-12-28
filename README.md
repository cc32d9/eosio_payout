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

The payer's RAM qupta is charged for creating all memory structures
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
