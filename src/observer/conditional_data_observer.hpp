#ifndef CONDITIONAL_DATA_OBSERVER_H
#define CONDITIONAL_DATA_OBSERVER_H

template <typename T, typename Condition>
class ConditionallyDataObserved;

template <typename T, typename Condition = void>
class ConditionalDataObserver {
    friend class ConditionallyDataObserved<T, Condition>;

    public:

        using Observed_Data = T;
        using Observing_Condition = Condition;

        typedef T ObsData;
        typedef Condition ObsCondition;

        virtual void update(ConditionallyDataObserved<T, Condition> *obs, Condition c, T *d) = 0;
};

template <typename T>
class ConditionalDataObserver<T, void> {
  friend class ConditionallyDataObserved<T, void>;

public:
  typedef T ObsData;

public:
  virtual void update(ConditionallyDataObserved<T, void> *obs, T *d) = 0;
};


#endif // CONDITIONAL_DATA_OBSERVER_H