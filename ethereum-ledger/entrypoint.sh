#!/bin/sh
set -e

echo "=== Starting Hardhat Node ==="
echo "RPC endpoint: http://0.0.0.0:8545"
echo ""

# Start the Hardhat node in the background
npx hardhat node --hostname 0.0.0.0 &
NODE_PID=$!

# Wait for the node to be ready
echo "Waiting for node to start..."
for i in $(seq 1 30); do
  if node -e "fetch('http://localhost:8545',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({jsonrpc:'2.0',method:'eth_blockNumber',params:[],id:1})}).then(r=>r.json()).then(d=>{if(d.result)process.exit(0);process.exit(1)}).catch(()=>process.exit(1))" 2>/dev/null; then
    echo "Node is ready!"
    break
  fi
  sleep 1
done

# Deploy contracts
echo ""
echo "=== Deploying Contracts ==="
npx hardhat run scripts/deploy.js --network localhost

# Copy deployment.json to host mount if available
if [ -d /host ]; then
  cp deployment.json /host/deployment.json 2>/dev/null && \
    echo "deployment.json exported to host" || true
fi

# Copy deployment.json and artifacts to shared volume for the UI container
if [ -d /shared ]; then
  cp deployment.json /shared/deployment.json 2>/dev/null && \
    echo "deployment.json exported to shared volume"
  cp -r artifacts /shared/artifacts 2>/dev/null && \
    echo "artifacts exported to shared volume"
fi

echo ""
echo "=== Hardhat Node Running ==="
echo "RPC: http://localhost:8545 (from host)"
echo "Contracts deployed and ready."
echo ""

# Keep the node process in the foreground
wait $NODE_PID
