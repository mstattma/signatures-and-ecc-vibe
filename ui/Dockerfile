FROM node:22-slim

# Install yarn (SE2 uses yarn workspaces)
RUN corepack enable && corepack prepare yarn@stable --activate

WORKDIR /app

# Copy SE2 project files
COPY package.json yarn.lock .yarnrc.yml ./
COPY .yarn/ ./.yarn/
COPY packages/hardhat/package.json ./packages/hardhat/package.json
COPY packages/nextjs/package.json ./packages/nextjs/package.json

# Install dependencies (slow but cached in Docker layer)
RUN yarn install --immutable || yarn install

# Copy the rest of the source
COPY . .

# The externalContracts.ts will be generated at runtime by the entrypoint
# (needs deployment.json from the node container)
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 3000

ENTRYPOINT ["/entrypoint.sh"]
