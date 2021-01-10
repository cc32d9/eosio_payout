#!/usr/bin/env node

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


const config    = require('config');
const fetch     = require('node-fetch');
const mariadb   = require('mariadb');

const { Api, JsonRpc, RpcError } = require('eosjs');
const { JsSignatureProvider } = require('eosjs/dist/eosjs-jssig');
const { TextEncoder, TextDecoder } = require('util');



const url        = config.get('url');
const engineacc  = config.get('engineacc');
const payeracc   = config.get('payeracc');
const payerkey   = config.get('payerkey');

const book_batch_size   = config.get('book_batch_size');

const schedule_name = config.get('schedule.name');
const currency = config.get('schedule.currency');
const currency_precision = config.get('schedule.currency_precision');
const currency_multiplier = 10**currency_precision;

const pool = mariadb.createPool(config.get('mariadb_server'));

const timer_keepalive = config.get('timer.keepalive');  // this one prints the keepalive message to console
const timer_check_accounts = config.get('timer.check_accounts'); // this one checks if new EOSIO accounts exist on chain
const timer_fullscan = config.get('timer.fullscan'); // this one scans all recipients
const timer_partialscan = config.get('timer.partialscan'); // this one scans only recipients with new payments


const sigProvider = new JsSignatureProvider([payerkey]);
const rpc = new JsonRpc(url, { fetch });
const api = new Api({rpc: rpc, signatureProvider: sigProvider,
                     textDecoder: new TextDecoder(), textEncoder: new TextEncoder()});

console.log("Started payer daemon using " + url);
console.log("Schedule name: " + schedule_name);

var full_scan_running = false;
var latest_payment_timestamp = 0;
var book_batch = new Array();

setInterval(keepaalive, timer_keepalive);
setTimeout(check_accounts, timer_check_accounts);
setTimeout(partialscan, timer_partialscan);

fullscan();


async function keepaalive() {
    console.log('keepalive');
}


async function fullscan() {
    full_scan_running = true;
    console.log("fullscan started");

    let booked_totals = new Map();
    let counter = 0;
    let conn;

    try {
        await fetch_booked_totals(booked_totals);

        conn = await pool.getConnection();
        conn.queryStream
        ("SELECT PAYMENTS.recipient AS recipient, SUM(amount) AS due, UNIX_TIMESTAMP(MAX(tstamp)) AS ts, memo " +
         "FROM PAYMENTS " +
         "INNER JOIN VALID_ACCOUNTS ON PAYMENTS.recipient=VALID_ACCOUNTS.account " +
         "LEFT JOIN PAYMENT_MEMO ON PAYMENTS.recipient=PAYMENT_MEMO.recipient " +
         "GROUP BY recipient")
            .on("error", err => {
                console.error(err);
            })
            .on("data", row => {
                if( ! booked_totals.has(row.recipient) ||
                    row.due > booked_totals.get(row.recipient) ) {

                    counter++;
                    
                    if( row.ts > latest_payment_timestamp ) {
                        latest_payment_timestamp = row.ts;
                    }

                    if( row.memo === null ) {
                        row.memo = '';
                    }

                    push_book_batch(row.recipient, row.due, row.memo);
                }
            })
            .on("end", () => {
                if( book_batch.size > 0 ) {
                    send_book_tx(book_batch.slice());
                    book_batch = new Array();
                }
                console.log("fullscan: sent " + counter + " bookings");
            });
    } catch (err) {
        console.error(err);
    } finally {
	if (conn) conn.release(); //release to pool
    }

    full_scan_running = false;
    setTimeout(fullscan, timer_fullscan);
}



async function partialscan() {
    if( !full_scan_running && latest_payment_timestamp > 0 ) {

        let counter = 0;
        let conn;

        try {
            conn = await pool.getConnection();
            conn.queryStream
            ("SELECT PAYMENTS.recipient AS recipient, SUM(amount) AS due, UNIX_TIMESTAMP(MAX(tstamp)) AS ts, memo " +
             "FROM PAYMENTS " +
             "INNER JOIN (SELECT DISTINCT recipient AS recipient FROM PAYMENTS " +
             "    WHERE tstamp > FROM_UNIXTIME(" + latest_payment_timestamp + ")) X ON PAYMENTS.recipient=X.recipient " +
             "INNER JOIN VALID_ACCOUNTS ON PAYMENTS.recipient=VALID_ACCOUNTS.account " +
             "LEFT JOIN PAYMENT_MEMO ON PAYMENTS.recipient=PAYMENT_MEMO.recipient " +
             "GROUP BY recipient")
                .on("error", err => {
                    console.error(err);
                })
                .on("data", row => {
                    counter++;
                    if( row.ts > latest_payment_timestamp ) {
                        latest_payment_timestamp = row.ts;
                    }

                    if( row.memo === null ) {
                        row.memo = '';
                    }

                    check_and_book({recipient: row.recipient,
                                    due: parseFloat(row.due),
                                    memo: row.memo});
                })
                .on("end", () => {
                    if( book_batch.size > 0 ) {
                        send_book_tx(book_batch.slice());
                        book_batch = new Array();
                    }
                    console.log("partialscan: found  " + counter + " new payments");
                });
        } catch (err) {
            console.error(err);
        } finally {
	    if (conn) conn.release(); //release to pool
        }
    }

    setTimeout(partialscan, timer_partialscan);
}


async function check_accounts() {
    let conn;    
    try {
        let counter = 0;
        conn = await pool.getConnection();
        let rows = await conn.query
        ("SELECT DISTINCT recipient AS recipient " +
         "FROM PAYMENTS " +
         "LEFT JOIN VALID_ACCOUNTS ON PAYMENTS.recipient=VALID_ACCOUNTS.account " +
         "WHERE VALID_ACCOUNTS.account IS NULL");

        for( row of rows ) {
            if( await check_if_valid_account(row.recipient) ) {
                await conn.query("INSERT INTO VALID_ACCOUNTS (account) VALUES (?)", [row.recipient]);
                counter++;
            }
        }
        
        console.log("check_accounts: found  " + counter + " new valid accounts out of " + rows.size);
    } catch (err) {
        console.error(err);
    } finally {
	if (conn) conn.release(); //release to pool
    }

    setTimeout(check_accounts, timer_check_accounts);
}




function push_book_batch(recipient, due, memo) {
    book_batch.push({recipient: recipient,
                     new_total: parseFloat(due).toFixed(currency_precision) + " " + currency,
                     memo: memo});
    if( book_batch.size >= book_batch_size ) {
        send_book_tx(book_batch.slice());
        book_batch = new Array();
    }
}



async function fetch_booked_totals(totals_map, nextkey="") {
    let response = await fetch(url + '/v1/chain/get_table_rows', {
        method: 'post',
        body:    JSON.stringify({
            json: 'true',
            code: engineacc,
            scope: schedule_name,
            table: 'recipients',
            key_type: 'name',
            lower_bound: nextkey
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    let data = await response.json();
    data.rows.forEach(function(row) {
        let amount = parseInt(row.booked_total)/currency_multiplier;
        totals_map.set(row.account, amount.toFixed(currency_precision));
    });

    if( data.more ) {
        return fetch_booked_totals(totals_map, data.next_key);
    }

    console.log("fetch_booked_totals: retrieved " + totals_map.size + " balances");
    return;
}


async function check_and_book(row) {
    let response = await fetch(url + '/v1/chain/get_table_rows', {
        method: 'post',
        body:    JSON.stringify({
            json: 'true',
            code: engineacc,
            scope: schedule_name,
            table: 'recipients',
            key_type: 'name',
            lower_bound: row.recipient,
            limit: 1
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    let data = await response.json();
    let amount = 0;
    if( data.rows.size > 0 ) {
        amount = parseInt(data.rows[0].booked_total)/currency_multiplier;
    }

    if( row.due > amount ) {
        push_book_batch(row.recipient, row.due, row.memo);
    }
}


async function send_book_tx(rows) {
    try {
        rows.forEach(function(row) {
            console.log("booking " + row.new_total + " for " + row.recipient + ", memo: " + row.memo);
        });
        
        const result = await api.transact(
            {
                actions:
                [
                    {
                        account: engineacc,
                        name: 'bookm',
                        authorization: [{
                            actor: payeracc,
                            permission: 'active'} ],
                        data: {
                            schedule_name: schedule_name,
                            records: rows
                        },
                    }
                ]
            },
            {
                blocksBehind: 10,
                expireSeconds: 60
            }
        );
        console.info("bookm transaction ID: " + result.transaction_id);
    } catch (e) {
        console.error('ERROR: ' + e);
    }
}


async function check_if_valid_account(acc) {
    let response = await fetch(url + '/v1/chain/get_account', {
        method: 'post',
        body:    JSON.stringify({
            account_name: acc,
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    return response.ok ? true:false;
}



/*
 Local Variables:
 mode: javascript
 indent-tabs-mode: nil
 End:
*/
