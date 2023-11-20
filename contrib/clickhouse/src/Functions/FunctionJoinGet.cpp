#include <Functions/FunctionJoinGet.h>

#include <Columns/ColumnString.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <Interpreters/Context.h>
#include <Interpreters/HashJoin.h>
#include <Storages/StorageJoin.h>


namespace DB
{
namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
}

template <bool or_null>
ColumnPtr ExecutableFunctionJoinGet<or_null>::executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t) const
{
    ColumnsWithTypeAndName keys;
    for (size_t i = 2; i < arguments.size(); ++i)
    {
        auto key = arguments[i];
        keys.emplace_back(std::move(key));
    }
    return storage_join->joinGet(keys, result_columns, getContext()).column;
}

template <bool or_null>
ExecutableFunctionPtr FunctionJoinGet<or_null>::prepare(const ColumnsWithTypeAndName &) const
{
    Block result_columns {{return_type->createColumn(), return_type, attr_name}};
    return std::make_unique<ExecutableFunctionJoinGet<or_null>>(getContext(), table_lock, storage_join, result_columns);
}

static std::pair<std::shared_ptr<StorageJoin>, String>
getJoin(const ColumnsWithTypeAndName & arguments, ContextPtr context)
{
    String join_name;
    if (const auto * name_col = checkAndGetColumnConst<ColumnString>(arguments[0].column.get()))
    {
        join_name = name_col->getValue<String>();
    }
    else
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                        "Illegal type {} of first argument of function joinGet, expected a const string.",
                        arguments[0].type->getName());

    auto qualified_name = QualifiedTableName::parseFromString(join_name);
    if (qualified_name.database.empty())
        qualified_name.database = context->getCurrentDatabase();

    auto table = DatabaseCatalog::instance().getTable({qualified_name.database, qualified_name.table}, std::const_pointer_cast<Context>(context));
    auto storage_join = std::dynamic_pointer_cast<StorageJoin>(table);
    if (!storage_join)
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Table {} should have engine StorageJoin", join_name);

    String attr_name;
    if (const auto * name_col = checkAndGetColumnConst<ColumnString>(arguments[1].column.get()))
    {
        attr_name = name_col->getValue<String>();
    }
    else
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                        "Illegal type {} of second argument of function joinGet, expected a const string.",
                        arguments[1].type->getName());
    return std::make_pair(storage_join, attr_name);
}

template <bool or_null>
FunctionBasePtr JoinGetOverloadResolver<or_null>::buildImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &) const
{
    if (arguments.size() < 3)
        throw Exception(
            ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
            "Number of arguments for function '{}' doesn't match: passed {}, should be greater or equal to 3",
            getName() , arguments.size());
    auto [storage_join, attr_name] = getJoin(arguments, getContext());
    DataTypes data_types(arguments.size() - 2);
    DataTypes argument_types(arguments.size());
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        if (i >= 2)
            data_types[i - 2] = arguments[i].type;
        argument_types[i] = arguments[i].type;
    }

    auto return_type = storage_join->joinGetCheckAndGetReturnType(data_types, attr_name, or_null || storage_join->useNulls());
    auto table_lock = storage_join->lockForShare(getContext()->getInitialQueryId(), getContext()->getSettingsRef().lock_acquire_timeout);

    if (storage_join->useNulls())
        return std::make_unique<FunctionJoinGet<true>>(getContext(), table_lock, storage_join, attr_name, argument_types, return_type);

    return std::make_unique<FunctionJoinGet<or_null>>(getContext(), table_lock, storage_join, attr_name, argument_types, return_type);
}

REGISTER_FUNCTION(JoinGet)
{
    // joinGet
    factory.registerFunction<JoinGetOverloadResolver<false>>();
    // joinGetOrNull
    factory.registerFunction<JoinGetOverloadResolver<true>>();
}

template class ExecutableFunctionJoinGet<true>;
template class ExecutableFunctionJoinGet<false>;
template class FunctionJoinGet<true>;
template class FunctionJoinGet<false>;
template class JoinGetOverloadResolver<true>;
template class JoinGetOverloadResolver<false>;
}