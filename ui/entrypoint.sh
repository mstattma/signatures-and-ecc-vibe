#!/bin/sh
set -e

echo "=== Scaffold-ETH 2 UI ==="

# Wait for deployment.json from the node container
echo "Waiting for deployment.json..."
for i in $(seq 1 60); do
  if [ -f /shared/deployment.json ]; then
    echo "Found deployment.json!"
    break
  fi
  if [ "$i" = "60" ]; then
    echo "WARNING: deployment.json not found after 60s, using defaults"
  fi
  sleep 1
done

# Generate externalContracts.ts from deployment.json + ABIs
if [ -f /shared/deployment.json ] && [ -d /shared/artifacts ]; then
  echo "Generating externalContracts.ts from deployed contracts..."
  node /app/generate-contracts.js /shared/deployment.json /shared/artifacts
else
  echo "Using existing externalContracts.ts (no deployment data available)"
fi

echo ""
echo "Starting Next.js dev server on http://0.0.0.0:3000"
echo "  Debug page: http://localhost:3000/debug"
echo ""

cd /app
exec yarn workspace @se-2/nextjs dev --hostname 0.0.0.0
