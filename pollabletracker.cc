/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "pollabletracker.h"
#include "pollable.h"

PollableTracker::PollableTracker() {

}

PollableTracker::~PollableTracker() {

}

void PollableTracker::RegisterPollable(std::shared_ptr<Pollable> in_pollable) {
    local_locker lock(&pollable_mutex);

    add_vec.push_back(in_pollable);
}

void PollableTracker::RemovePollable(std::shared_ptr<Pollable> in_pollable) {
    local_locker lock(&pollable_mutex);

    remove_map[in_pollable] = 1;
}

void PollableTracker::Maintenance() {
    local_locker lock(&pollable_mutex);

    for (auto r : remove_map) {
        for (auto i = pollable_vec.begin(); i != pollable_vec.end(); ++i) {
            if (r.first == *i) {
                pollable_vec.erase(i);
                break;
            }
        }
    }

    for (auto i = add_vec.begin(); i != add_vec.end(); ++i) {
        pollable_vec.push_back(*i);
    }

    remove_map.clear();
    add_vec.clear();
}

void PollableTracker::Selectloop(bool spindown_mode) {
    int max_fd;
    fd_set rset, wset;
    struct timeval tm;
    int consec_badfd = 0;

    time_t shutdown_time = time(0) + 3;

    // Core loop
    while (1) {
        if (spindown_mode && time(0) > shutdown_time)
            break;

        if ((!spindown_mode && Globalreg::globalreg->spindown) || 
                Globalreg::globalreg->fatal_condition ||
                Globalreg::globalreg->complete) 
            break;

        tm.tv_sec = 0;
        tm.tv_usec = 100000;

        Maintenance();

        max_fd = MergePollableFds(&rset, &wset);

        if (select(max_fd + 1, &rset, &wset, NULL, &tm) < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                if (errno == EBADF) {
                    consec_badfd++;

                    if (consec_badfd > 20) 
                        throw std::runtime_error(fmt::format("select() > 20 consecutive badfd errors, latest {} {}",
                                    errno, strerror(errno)));

                    continue;
                } else {
                    throw std::runtime_error(fmt::format("select() failed: {} {}", errno, strerror(errno)));
                }
            }
        }

        consec_badfd = 0;

        // Run maintenance again so we don't gather purged records after the select()
        Maintenance();

        ProcessPollableSelect(rset, wset);
    }
}

int PollableTracker::MergePollableFds(fd_set *rset, fd_set *wset) {
    int max_fd = 0;

    FD_ZERO(rset);
    FD_ZERO(wset);

    for (auto i : pollable_vec) {
        max_fd = i->MergeSet(max_fd, rset, wset);
    }

    return max_fd;
}

int PollableTracker::ProcessPollableSelect(fd_set rset, fd_set wset) {
    int r;
    int num = 0;

    for (auto i : pollable_vec) {
        r = i->Poll(rset, wset);

        if (r >= 0)
            num++;
    }

    return num;
}

