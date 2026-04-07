sed -i 's/g_query_cache.put(query, cache_buffer);//g' src/server/server.cpp
sed -i 's/if (g_query_cache.get(query, cached_result)) {/if (false) {/g' src/server/server.cpp
make clean && make build/server && rm -rf tables && killall server; ./build/server & sleep 1; make benchmark
