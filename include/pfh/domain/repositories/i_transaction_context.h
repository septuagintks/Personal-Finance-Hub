// Personal Finance Hub - Transaction Context Interface
// Version: 1.0
// C++23
//
// Opaque handle representing a single database transaction. Domain and
// Application code never inspect the underlying driver object.

#pragma once

namespace pfh::domain {

class ITransactionContext {
public:
    virtual ~ITransactionContext() = default;
};

} // namespace pfh::domain
