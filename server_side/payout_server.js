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

const { Api, JsonRpc, RpcError } = require('eosjs');
const { JsSignatureProvider } = require('eosjs/dist/eosjs-jssig');
const { TextEncoder, TextDecoder } = require('util');



const url        = config.get('url');
const engineacc  = config.get('engineacc');
const adminacc   = config.get('adminacc');
const adminkey   = config.get('adminkey');
const runpayouts_limit = config.get('limit.runpayouts');
const approvals_limit = config.get('limit.approvals');

const timer_keepalive = config.get('timer.keepalive');
const timer_check_dues = config.get('timer.check_dues');
const timer_check_dues_followup = config.get('timer.check_dues_followup');
const timer_check_unapproved = config.get('timer.check_unapproved');
const timer_check_approved = config.get('timer.check_approved');

const timer_recheck_hash = config.get('timer.recheck_hash');
const timer_push_approved_list = config.get('timer.push_approved_list');


const sigProvider = new JsSignatureProvider([adminkey]);
const rpc = new JsonRpc(url, { fetch });
const api = new Api({rpc: rpc, signatureProvider: sigProvider,
                     textDecoder: new TextDecoder(), textEncoder: new TextEncoder()});


var last_revision = 0;
var has_code_map = new Map();
var approved_list = new Array();

setInterval(keepaalive, timer_keepalive);
setInterval(check_dues, timer_check_dues);
setInterval(check_unapproved, timer_check_unapproved);
setInterval(check_approved, timer_check_approved);
setInterval(push_approved_list, timer_push_approved_list);


async function keepaalive() {
    console.log('keepalive');
}


async function check_dues() {
    // console.log("check_dues() started");

    if( await has_open_dues() ) {
        // console.log("due payments found");
        let new_revision = await get_revision();
        if( new_revision != last_revision ) {
            console.log("found an updated revision: " + new_revision);
            last_revision = new_revision;
            runpayouts();
            setTimeout(check_dues, timer_check_dues_followup);
        }
        else {
            // console.log("revision has not updated");
        }
    }
    else {
        // console.log("no due payments found");
    }
}


async function check_unapproved() {
    // console.log("check_unapproved() started");
    let now = new Date();    
    fetch_approved(false, async function (acc) {
        if( !has_code_map.has(acc) || now >= has_code_map.get(acc) + timer_recheck_hash ) {
            let hc = await has_code(acc);
            if( !hc ) {
                approved_list.push(acc);
            }
            else {
                has_code_map.set(acc, now);
            }
        }
    });
}


async function push_approved_list() {
    if( approved_list.length > 0 ) {
        let clean = approved_list.slice(0, approvals_limit);
        approved_list.splice(0, approvals_limit);   
        console.log("approving " + clean.length + " accounts");
        await approve(true, clean);
    }
}


async function check_approved() {
    // console.log("check_approved() started");
    let now = new Date();    
    fetch_approved(true, async function (acc) {
        let hc = await has_code(acc);
        if( hc ) {
            has_code_map.set(acc, now);
            approve(false, [acc]);
        }
    });
}





// checks if there any schedules with due payments
async function has_open_dues() {
    let response = await fetch(url + '/v1/chain/get_table_rows', {
        method: 'post',
        body:    JSON.stringify({
            json: 'true',
            code: engineacc,
            scope: '0',
            table: 'schedules',
            index_position: '2',
            key_type: 'i64',
            lower_bound: 1,
            limit: 1
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    let data = await response.json();
    return (data.rows.length > 0) ? true : false;
}


async function get_revision() {
    let response = await fetch(url + '/v1/chain/get_table_rows', {
        method: 'post',
        body:    JSON.stringify({
            json: 'true',
            code: engineacc,
            scope: '0',
            table: 'props',
            index_position: '1',
            key_type: 'name',
            lower_bound: "revision",
            limit: 1
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    let data = await response.json();
    return (data.rows.length > 0) ? data.rows[0].val_uint : 0;
}
    

async function runpayouts() {
    try {
        const result = await api.transact(
            {
                actions:
                [
                    {
                        account: engineacc,
                        name: 'runpayouts',
                        authorization: [{
                            actor: adminacc,
                            permission: 'active'} ],
                        data: {
                            count: runpayouts_limit
                        },
                    }
                ]
            },
            {
                blocksBehind: 10,
                expireSeconds: 60
            }
        );
        console.info('runpayouts transaction ID: ', result.transaction_id);
    } catch (e) {
        console.error('ERROR: ' + e);
    }
}


async function fetch_approved(check_approved, callback, idx_low=1) {
    let response = await fetch(url + '/v1/chain/get_table_rows', {
        method: 'post',
        body:    JSON.stringify({
            json: 'true',
            code: engineacc,
            scope: '0',
            table: 'approvals',
            index_position: check_approved?'2':'3',
            key_type: 'i64',
            lower_bound: idx_low
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    let data = await response.json();
    data.rows.forEach(function(row) {
        callback(row.account);
    });
    
    if( data.more ) {
        return fetch_approved(check_approved, callback, data.next_key);
    }
    
    return;
}


async function has_code(acc) {
    let response = await fetch(url + '/v1/chain/get_code_hash', {
        method: 'post',
        body:    JSON.stringify({
            json: 'true',
            account_name: acc
        }),
        headers: { 'Content-Type': 'application/json' },
    });

    let data = await response.json();
    let ret = true;
    if( data.code_hash == '0000000000000000000000000000000000000000000000000000000000000000' ) {
        ret = false;
    }

    console.log("account " + acc + " has code: " + ret);
    return ret;
}


async function approve(do_approve, accounts) {
    try {
        let action = do_approve ? 'approveacc':'unapproveacc';
        const result = await api.transact(
            {
                actions:
                [
                    {
                        account: engineacc,
                        name: action,
                        authorization: [{
                            actor: adminacc,
                            permission: 'active'} ],
                        data: {
                            accounts: accounts
                        },
                    }
                ]
            },
            {
                blocksBehind: 10,
                expireSeconds: 60
            }
        );
        console.info(action + '{' + accounts.length + '} transaction ID: ', result.transaction_id);
    } catch (e) {
        console.error('ERROR: ' + e);
    }

}




/*
 Local Variables:
 mode: javascript
 indent-tabs-mode: nil
 End:
*/
