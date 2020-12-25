CONTRACT=payout

all: $(CONTRACT).wasm $(CONTRACT).abi

%.wasm: %.cpp
	eosio-cpp -I. -o $@ $<

%.abi: %.cpp
	eosio-abigen -contract=$(CONTRACT) --output=$@ $<

clean:
	rm -f $(CONTRACT).wasm $(CONTRACT).abi
