import { ImageSearch } from "./_components/ImageSearch";
import type { NextPage } from "next";
import { getMetadata } from "~~/utils/scaffold-eth/getMetadata";

export const metadata = getMetadata({
  title: "Images",
  description: "Search registered image records on-chain",
});

const Images: NextPage = () => {
  return (
    <>
      <div className="text-center mt-8 mb-4">
        <h1 className="text-4xl font-bold">Registered Images</h1>
        <p className="text-neutral mt-2">
          Search image records by signature prefix or by `(pHash, salt)`.
          <br />
          This view is active on chains where the EAS resolver has been deployed.
        </p>
      </div>
      <ImageSearch />
    </>
  );
};

export default Images;
