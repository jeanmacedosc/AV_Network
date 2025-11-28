#ifndef CONDITIONALLY_DATA_OBSERVED_H
#define CONDITIONALLY_DATA_OBSERVED_H

#include<vector>

template <typename T, typename Condition>
class ConditionalDataObserver;

template <typename T, typename Condition = void>
class ConditionallyDataObserved
{
friend class ConditionalDataObserver<T, Condition>;

public:
    using ObsData = T;
    using Observer = ConditionalDataObserver<T, Condition>;
    using ObsList = std::vector<Observer*>;
    using ConList = std::vector<Condition>;

    virtual void attach(Observer * o, Condition c) {
        _observers.insert(_observers.begin(), o);
        _conditions.insert(_conditions.begin(), c);
    }

    virtual void detach(Observer * o, Condition c) {
        // remove all entries where both observer and condition match
        for (std::size_t i = _observers.size(); i-- > 0; ) {
            if (_observers[i] == o && _conditions[i] == c) {
                _observers.erase(_observers.begin() + i);
                _conditions.erase(_conditions.begin() + i);
            }
        }
    }

    virtual bool notify(Condition c, ObsData *d) {
        bool n = false;
        std::size_t size = _observers.size();

        for (std::size_t i = 0; i < size; ++i) {
            // std::cout << "DEBUG: Comparing _conditions[" << i << "] ("
            //   << _conditions[i]
            //   << ", type: " << typeid(_conditions[i]).name() << ")"
            //   << " with c ("
            //   << c
            //   << ", type: " << typeid(c).name() << ")" << std::endl;

            if (_conditions[i] == c) {
                if (_observers[i]) {
                    _observers[i]->update(this, c, d);
                    n = true;
                }
            }
        }

        return n;
    }

private:
    ObsList _observers;
    ConList _conditions;

};

/**
 * @brief Unconditional
 */
template <typename T>
class ConditionallyDataObserved<T, void>
{
friend class ConditionalDataObserver<T, void>;

public:
    using ObsData = T;
    using Observer = ConditionalDataObserver<T, void>;
    using ObsList = std::vector<Observer*>;

    // Attach now only takes an observer
    virtual void attach(Observer * o) {
        _observers.insert(_observers.begin(), o);
    }

    // Detach now only takes an observer
    virtual void detach(Observer * o) {
        // Remove all entries where the observer matches
        for (std::size_t i = _observers.size(); i-- > 0; ) {
            if (_observers[i] == o) {
                _observers.erase(_observers.begin() + i);
            }
        }
    }

    // Notify now only takes data
    virtual bool notify(ObsData *d) {
        bool n = false;
        std::size_t size = _observers.size();

        for (std::size_t i = 0; i < size; ++i) {
            if (_observers[i]) {
                // Call the specialized update()
                _observers[i]->update(this, d); 
                n = true;
            }
        }
        return n;
    }

private:
    ObsList _observers; // Only one list
};

#endif // CONDITIONALLY_DATA_OBSERVED_H