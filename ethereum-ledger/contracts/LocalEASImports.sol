// SPDX-License-Identifier: MIT
pragma solidity ^0.8.27;

// This file exists only so Hardhat compiles the EAS and SchemaRegistry
// artifacts from the dependency package, enabling local deployment on the
// Hardhat/localhost network.

import { EAS } from "@ethereum-attestation-service/eas-contracts/contracts/EAS.sol";
import { SchemaRegistry } from "@ethereum-attestation-service/eas-contracts/contracts/SchemaRegistry.sol";

contract LocalEASImports {
    function _touch(EAS, SchemaRegistry) external pure {}
}
