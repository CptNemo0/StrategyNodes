#ifdef __cpp_contracts

#include <contracts>

void handle_contract_violation(
    const std::contracts::contract_violation& violation) {
  std::contracts::invoke_default_contract_violation_handler(violation);
}

#endif  // __cpp_contracts
