"use client";

import type { NextPage } from "next";
import { ReputationView } from "./_components/ReputationView";

const ReputationPage: NextPage = () => {
  return (
    <div className="container mx-auto">
      <h1 className="text-3xl font-bold text-center mt-8 mb-4">Reputation Registry</h1>
      <p className="text-center text-base-content/70 mb-6 max-w-2xl mx-auto">
        View attester reputation scores, endorsement history, and dispute records.
        Reputation is earned through attestations and community endorsements.
      </p>
      <ReputationView />
    </div>
  );
};

export default ReputationPage;
