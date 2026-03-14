import { KeyList } from "./_components/KeyList";
import type { NextPage } from "next";
import { getMetadata } from "~~/utils/scaffold-eth/getMetadata";

export const metadata = getMetadata({
  title: "Key Registry",
  description: "View and manage stego signing keys",
});

const Keys: NextPage = () => {
  return (
    <>
      <div className="text-center mt-8 mb-4">
        <h1 className="text-4xl font-bold">Key Registry</h1>
        <p className="text-neutral mt-2">
          View registered signing keys, register new keys, and revoke compromised keys.
          <br />
          Each address can have multiple keys but only one active at a time.
        </p>
      </div>
      <KeyList />
    </>
  );
};

export default Keys;
