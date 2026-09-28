#pragma once

#include <VLMS/Core/Result.h>

#include <cstdio>
#include <string>

namespace VLMS::RepoSql {

inline Status sqlFailure(const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return Status::fail(ErrorKind::Sql, "error.sql");
}

template<typename T>
inline Result<T> sqlResult(const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return Result<T>::fail(ErrorKind::Sql, "error.sql");
}

inline Status validation(std::string key, std::string detail = {})
{
    return Status::fail(ErrorKind::Validation, std::move(key), std::move(detail));
}

inline Status notFound(std::string key)
{
    return Status::fail(ErrorKind::NotFound, std::move(key));
}

template<typename T>
inline Result<T> notFoundResult(std::string key)
{
    return Result<T>::fail(ErrorKind::NotFound, std::move(key));
}

template<typename T>
inline Result<T> validationResult(std::string key, std::string detail = {})
{
    return Result<T>::fail(ErrorKind::Validation, std::move(key), std::move(detail));
}

}  // namespace VLMS::RepoSql
