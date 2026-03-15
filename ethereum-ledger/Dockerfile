FROM node:22-slim

WORKDIR /app

# Copy package files and install dependencies
COPY package.json package-lock.json ./
RUN npm install --ignore-scripts

# Copy contracts and config
COPY hardhat.config.js ./
COPY contracts/ ./contracts/

# Compile contracts
RUN npx hardhat compile

# Copy scripts (for deploy on startup)
COPY scripts/ ./scripts/

# Expose Hardhat node RPC port
EXPOSE 8545

# Entrypoint: start Hardhat node, deploy contracts, then keep node alive
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
