#include <catch2/catch_test_macros.hpp>

#include <core/SwapbackArray.h>

TEST_CASE("SwapbackArray RemoveAt on the last element just pops, no swap needed", "[SwapbackArray]")
{
    SwapbackArray<int> arr;
    for (int v : {1, 2, 3})
        arr.Pushback(v);

    arr.RemoveAt(2); // last index

    REQUIRE(arr.Size() == 2);
    REQUIRE(arr[0] == 1);
    REQUIRE(arr[1] == 2);
}

TEST_CASE("SwapbackArray RemoveAt on a non-last element swaps the last element into its place",
          "[SwapbackArray]")
{
    SwapbackArray<int> arr;
    for (int v : {1, 2, 3, 4})
        arr.Pushback(v);

    arr.RemoveAt(1); // removes 2

    REQUIRE(arr.Size() == 3);
    REQUIRE(arr[1] == 4); // former last element swapped into the removed slot
    REQUIRE(arr[0] == 1);
    REQUIRE(arr[2] == 3);
}

TEST_CASE("SwapbackArray Erase of a value not present throws", "[SwapbackArray]")
{
    SwapbackArray<int> arr;
    for (int v : {1, 2, 3})
        arr.Pushback(v);

    REQUIRE_THROWS_AS(arr.Erase(42), std::runtime_error);
    REQUIRE(arr.Size() == 3); // failed erase must not mutate the array
}

TEST_CASE("SwapbackArray Erase of a present value removes exactly one instance", "[SwapbackArray]")
{
    SwapbackArray<int> arr;
    for (int v : {1, 2, 3})
        arr.Pushback(v);

    arr.Erase(2);

    REQUIRE(arr.Size() == 2);
    REQUIRE_THROWS_AS(arr.Erase(2), std::runtime_error);
}

TEST_CASE("SwapbackArray iteration after removal visits exactly the remaining elements",
          "[SwapbackArray]")
{
    SwapbackArray<int> arr;
    for (int v : {1, 2, 3, 4, 5})
        arr.Pushback(v);

    arr.RemoveAt(0); // swaps 5 into slot 0: {5, 2, 3, 4}

    size_t count = 0;
    int sum = 0;
    for (int v : arr)
    {
        ++count;
        sum += v;
    }

    REQUIRE(count == arr.Size());
    REQUIRE(count == 4);
    REQUIRE(sum == 5 + 2 + 3 + 4);
}
