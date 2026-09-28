#pragma once

#include <VLMS/Core/Result.h>

#include <cstdio>
#include <string>

namespace VLMS::Repositories::RepoSql {

inline Core::Status sqlFailure(const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return Core::Status::fail(Core::ErrorKind::Sql, "error.sql");
}

template<typename T>
inline Core::Result<T> sqlResult(const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return Core::Result<T>::fail(Core::ErrorKind::Sql, "error.sql");
}

inline Core::Status validation(std::string key, std::string detail = {})
{
    return Core::Status::fail(Core::ErrorKind::Validation, std::move(key), std::move(detail));
}

inline Core::Status notFound(std::string key)
{
    return Core::Status::fail(Core::ErrorKind::NotFound, std::move(key));
}

template<typename T>
inline Core::Result<T> notFoundResult(std::string key)
{
    return Core::Result<T>::fail(Core::ErrorKind::NotFound, std::move(key));
}

template<typename T>
inline Core::Result<T> validationResult(std::string key, std::string detail = {})
{
    return Core::Result<T>::fail(Core::ErrorKind::Validation, std::move(key), std::move(detail));
}

}  // namespace VLMS::Repositories::RepoSql
