# Telos testnet

alias tTcleos='cleos -v -u https://testnet.persiantelos.com'


tTcleos push action payoutengine newschedule '["payoutpayer1", "payer1sc1", "eosio.token", "1.0000 TLOS", "Welcome to the test"]' -p payoutpayer1
sleep 2

tTcleos push action payoutengine newschedule '["payoutpayer1", "payer1sc2", "eosio.token", "1.0000 TLOS", "Welcome to the test"]' -p payoutpayer1
sleep 2

tTcleos push action payoutengine newschedule '["payoutpayer2", "payer2sc1", "eosio.token", "1.0000 TLOS", "Welcome to the test"]' -p payoutpayer2
sleep 2

tTcleos push action payoutengine newschedule '["payoutpayer2", "payer2sc2", "btc.ptokens", "0.00000000 PBTC", "PBTC test"]' -p payoutpayer2
sleep 2


tTcleos transfer payoutpayer1 payoutengine "20.0000 TLOS"
sleep 2

tTcleos push action btc.ptokens transfer '["payoutpayer2", "payoutengine", "0.01000000 PBTC", "Deposit"]' -p payoutpayer2
sleep 2

tTcleos transfer payoutpayer2 payoutengine "20.0000 TLOS"
sleep 2


tTcleos push action payoutengine book '["payer1sc1", [["payoutrcpt11", "3.0000 TLOS"], ["payoutrcpt12", "4.0000 TLOS"], ["payoutrcpt13", "3.0000 TLOS"]]]' -p payoutpayer1
sleep 2

tTcleos push action payoutengine book '["payer1sc2", [["payoutrcpt11", "0.0100 TLOS"], ["payoutrcpt12", "0.0100 TLOS"], ["payoutrcpt13", "0.0100 TLOS"], ["payoutrcpt14", "0.0100 TLOS"]]]' -p payoutpayer1
sleep 2

tTcleos push action payoutengine book '["payer2sc1", [["payoutrcpt11", "0.0010 TLOS"], ["payoutrcpt12", "0.0010 TLOS"], ["payoutrcpt13", "0.0010 TLOS"], ["payoutrcpt14", "0.0010 TLOS"]]]' -p payoutpayer2
sleep 2

tTcleos push action payoutengine book '["payer2sc2", [["payoutrcpt12", "0.00010000 PBTC"], ["payoutrcpt13", "0.00010000 PBTC"], ["payoutrcpt14", "0.00010000 PBTC"]]]' -p payoutpayer2
sleep 2

tTcleos push action payoutengine approveacc '[["payoutrcpt11","payoutrcpt12","payoutrcpt13"]]' -p payoutadmin1
sleep 2


tTcleos push action payoutengine runpayouts '[10]' -p payoutrcpt11
sleep 2










 
