/*
 * AIEngine a new generation network intrusion detection system.
 *
 * Copyright (C) 2013-2018  Luis Campo Giralte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Ryadnology Team; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Ryadnology Team, 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * Written by Luis Campo Giralte <me@ryadpasha.com> 
 *
 */
#ifndef SRC_DATABASEADAPTOR_H_
#define SRC_DATABASEADAPTOR_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <fstream>

namespace aiengine {

class DatabaseAdaptor 
{
public:
	DatabaseAdaptor() {}
    	virtual ~DatabaseAdaptor() {}

	virtual void connect(std::string &connection_str) = 0;
	virtual void insert(const std::string &key) = 0;
	virtual void update(const std::string &key, const std::string &data) = 0;
	virtual void remove(const std::string &key) = 0;
};

typedef std::shared_ptr <DatabaseAdaptor> DatabaseAdaptorPtr;
typedef std::weak_ptr <DatabaseAdaptor> DatabaseAdaptorPtrWeak;

} // namespace aiengine

#endif  // SRC_DATABASEADAPTOR_H_
