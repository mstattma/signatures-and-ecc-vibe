import { UserList } from "./_components/UserList";
import type { NextPage } from "next";
import { getMetadata } from "~~/utils/scaffold-eth/getMetadata";

export const metadata = getMetadata({
  title: "Users",
  description: "View all registered users and their signing keys",
});

const Users: NextPage = () => {
  return (
    <>
      <div className="text-center mt-8 mb-4">
        <h1 className="text-4xl font-bold">Users</h1>
        <p className="text-neutral mt-2">
          All addresses that have registered signing keys on the KeyRegistry.
          <br />
          Click a user to view their full key history.
        </p>
      </div>
      <UserList />
    </>
  );
};

export default Users;
