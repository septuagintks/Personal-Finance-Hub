# Personal Finance Hub - Test Naming Convention

## Test Naming Standard

All tests must follow this naming pattern:

```
<ClassName>_When<Condition>_<ExpectedBehavior>
```

### Examples

#### Unit Tests (Domain Layer)
```cpp
// Money tests
Money_WhenAddingSameCurrency_ReturnsCorrectSum
Money_WhenAddingDifferentCurrency_ReturnsError

// ExchangeRate tests
ExchangeRate_WhenCalculatingReverse_ReturnsAccurateInverse
ExchangeRate_WhenMissingRate_ReturnsError

// TransferAggregate tests
TransferAggregate_WhenOutgoingAndRateProvided_CalculatesIncoming
TransferAggregate_WhenSameCurrency_RequiresEqualAmounts
```

#### Integration Tests (Repository)
```cpp
// Repository tests
AccountRepository_WhenSavingAccount_PersistsCorrectly
TransactionRepository_WhenQueryingByUser_ReturnsOnlyUserTransactions
ExchangeRateRepository_WhenInsertingDuplicate_AppendsNewRecord
```

#### API Tests
```cpp
// Controller tests
CreateTransfer_WhenValidInput_Returns201
CreateTransfer_WhenUnauthorized_Returns401
CreateTransfer_WhenInvalidAmount_Returns400
CreateTransfer_WhenAmountMismatch_Returns422
```

#### Business Rule Tests
```cpp
// Scenario-based tests
CashFlowReport_WhenTransferExists_ExcludesTransferFromIncomeAndExpense
DangerousDeleteAccount_WhenConfirmed_CleansDataAndEmitsOutboxEvent
ImportSync_WhenExternalTransactionRepeated_SkipsDuplicate
```

## Test Data Organization

```
tests/support/
├── fixtures/          # Test fixtures and setup helpers
├── builders/          # Test data builders
├── assertions/        # Custom assertions
└── data/             # Static test data files
```

## Test Path Categories

1. **Normal Path**: Expected success scenarios
2. **Boundary Path**: Edge cases and limits
3. **Error Path**: Failure scenarios and validation

Each high-risk component should have coverage across all three paths.
