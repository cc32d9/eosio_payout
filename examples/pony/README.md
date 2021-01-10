# Payout Engine client example

## Test Token on Telos Testnet

```
Token contract: ponytokenxxx
Symbol: 4,PONY
Max supply: 10000000.0000 PONY

Mint account: ponymintmint
Treasury account: ponytreasure
Payer account: ponypayerxxx


alias tTcleos='cleos -v -u https://testnet.persiantelos.com'

# Assuming EOSIO 2.x and CDT 1.7.0 packages are installed, see
# https://github.com/EOSIO/eos/releases
# https://github.com/EOSIO/eosio.cdt/releases

# Assuming the private keys are imported in a wallet and it's unlocked.

# compile the token contract
mkdir /opt/src
cd /opt/src
git clone https://github.com/EOSIO/eosio.contracts.git
cd eosio.contracts/contracts/eosio.token/src
eosio-cpp -I ../include/ eosio.token.cpp

# upload the token contract to the blockchain account
tTcleos system buyram ponytokenxxx ponytokenxxx -k 100
tTcleos set contract ponytokenxxx . eosio.token.wasm eosio.token.abi 

# we delegate the "create" action to ponymintmint, so that it can create new currencies
tTcleos set account permission ponytokenxxx create '{"threshold": 1, "accounts":[{"permission":{"actor":"ponymintmint","permission":"active"},"weight":1}], "waits":[] }' active -p ponytokenxxx@active
tTcleos set action permission ponytokenxxx ponytokenxxx create create -p ponytokenxxx@active

# make the token contract immutable
tTcleos set account permission ponytokenxxx active '{"threshold": 1, "accounts":[{"permission":{"actor":"eosio","permission":"active"},"weight":1}], "waits":[] }' owner -p ponytokenxxx@owner
tTcleos set account permission ponytokenxxx owner '{"threshold": 1, "accounts":[{"permission":{"actor":"eosio","permission":"active"},"weight":1}], "waits":[] }' -p ponytokenxxx@owner


# Create the token
tTcleos push action ponytokenxxx create '["ponymintmint", "10000000.0000 PONY"]' -p ponytokenxxx@create

# Issue 1M tokens and send them to treasury
tTcleos push action ponytokenxxx issue '["ponymintmint", "1000000.0000 PONY", ""]' -p ponymintmint@active
tTcleos push action ponytokenxxx transfer '["ponymintmint", "ponytreasure", "1000000.0000 PONY", "treasury issuance"]' -p ponymintmint

# allocate 100k tokens to the payer account
tTcleos push action ponytokenxxx transfer '["ponytreasure", "ponypayerxxx", "100000.0000 PONY", "preparing for payout"]' -p ponytreasure
```

## Setting up the schedule

```
# buy enough RAM for the payments
tTcleos system buyram ponypayerxxx ponypayerxxx -k 1000

# register the schedule
tTcleos push action payoutengine newschedule '["ponypayerxxx", "pony", "ponytokenxxx", "1.0000 PONY", "A pony!"]' -p ponypayerxxx
```

# top-up the payer engine
tTcleos push action ponytokenxxx transfer '["ponypayerxxx", "payoutengine", "10000.0000 PONY", "top-up"]' -p ponypayerxxx



## Payer server environment

```
apt update && apt install -y mariadb-server git curl

curl -sL https://deb.nodesource.com/setup_14.x | bash -
apt-get install -y nodejs


cd /opt
git clone https://github.com/cc32d9/eosio_payout.git
cd eosio_payout

mysql < examples/pony/ponypayer_dbsetup.sql
mysql --database ponypayer < payer_daemon/payer_tables.sql

cd payer_daemon
npm install

mkdir config
cat >config/default.json <<'EOT'
{
    "url": "https://testnet.persiantelos.com",
    "engineacc": "payoutengine",
    "payeracc": "ponypayerxxx",
    "payerkey": "5*************************************",

    "book_batch_size": 30,

    "schedule": {
        "name": "pony",
        "currency": "PONY",
        "currency_precision": 4
    },

    "mariadb_server": {
        "host": "localhost",
        "user": "ponypayer",
        "password": "ijnew228dfvds",
        "database": "ponypayer",
        "connectionLimit": 5
    },
    
    "timer": {
        "keepalive": 3600000,
        "check_accounts": 20000,
        "fullscan": 20000,
        "partialscan": 5000
    }
}
EOT

node payer_daemon.js
```

The node process will scan the PAYMENTS table periodically and book
new payments if needed.

