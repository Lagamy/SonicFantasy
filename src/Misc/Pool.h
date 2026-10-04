#pragma once 
#include "PoolId.h"
#include "Constants.h"
#include <deque>
#include <vector> 
#include <cstdint>
#include <stdexcept>
#include <sstream>
#include <format>


template <typename T> 
struct Pool {
	/* A deque: adding an element never moves the others, so a reference into the pool stays valid until its slot is removed (and reused); across anything that removes, keep the PoolId. */
	std::deque<T> objects;
	std::vector<bool> alive;
	std::vector<uint32_t> generation; 
	std::vector<uint32_t> freeSlots;	
	std::string name;
	uint32_t elementCount = 0; 

	Pool(const char* name_) : name(name_){};
	Pool() {};
	void isPoolIdValid(PoolId pId_)
	{
		if(pId_ == UninitializedPoolId)
		{
			throw std::runtime_error(std::format("{} Pool: UninitializedPoolId can't be used in get function.", this->name));
		}

		if(pId_.id >= this->objects.size() || !alive[pId_.id])
		{
			throw std::runtime_error(std::format("{} Pool: object with id: {} doesn't exist.",this->name, pId_.id));
		}
		if(this->generation[pId_.id] != pId_.generation)
		{
			throw std::runtime_error(std::format("{} Pool: slot with id: {} has bigger generation({}), than passed one({}).", this->name, pId_.id, pId_.generation, this->generation[pId_.id]));
		}
	}

	template <typename... Args>
    PoolId add(Args&&... args)
    {
        PoolId id = {0, 0};

        if (freeSlots.empty())
        {
            id.id = static_cast<uint32_t>(objects.size());
            this->objects.emplace_back(std::forward<Args>(args)...);
            this->generation.emplace_back(id.generation);
			this->alive.emplace_back(true); 
        }
        else
        {
            id.id = freeSlots.back();
            this->freeSlots.pop_back();
            // reconstruct in-place
			this->objects[id.id] = T(std::forward<Args>(args)...);
            id.generation = generation[id.id];
			this->alive[id.id] = true; 
        }
		this->elementCount++; 
        return id;
    }

	void remove(PoolId pId_) {
		#ifdef ENGINE_DEBUG
			this->isPoolIdValid(pId_);
		#endif 
		this->generation[pId_.id]++; 
		if constexpr (requires (T& obj) { obj.destroy(); })
		{
			objects[pId_.id].destroy();
		}
		this->freeSlots.emplace_back(pId_.id);
		this->alive[pId_.id] = false; 
		this->elementCount--; 
	}

	void resize(size_t size_) // Note: doesn't work for objects without default constructor 
	{
		#ifdef ENGINE_DEBUG
			if(!this->objects.empty())
			{
				throw std::runtime_error(std::format("{} Pool: can't resize Pool that was already initialized.", this->name));
			}
		#endif

		this->objects.resize(size_); 
		this->alive.resize(size_); 
		this->generation.resize(size_); 
	}
	
	void removeInternal(uint32_t id_) // Used mainly during itteration  
	{
		if(this->alive[id_])
		{
			this->generation[id_]++; 
			if constexpr (requires (T& obj) { obj.destroy(); })
			{
    			objects[id_].destroy();
			}
			this->freeSlots.emplace_back(id_);
			this->alive[id_] = false; 
		}
	}

	void clear() 
	{
		for(uint32_t i = 0; i < this->objects.size(); i++)
		{
			this->removeInternal(i); 
		}
		this->elementCount = 0; 
	}

	T& getInternal(uint32_t id_)
	{
		return this->objects[id_];
	}
	
	T& operator[](PoolId pId_)
	{
		#ifdef ENGINE_DEBUG
			this->isPoolIdValid(pId_); 
		#endif
		return this->objects[pId_.id];
    }

    const T& operator[](PoolId pId_) const 
	{
    	#ifdef ENGINE_DEBUG
			this->isPoolIdValid(pId_); 
		#endif
		return this->objects[pId_.id];
	}

	T& back()
	{
		#ifdef ENGINE_DEBUG
			if(this->objects.empty())
			{
				throw std::runtime_error(std::format("{} Pool: Can't use back() on empty Pool.", this->name));
			}
		#endif 

		uint32_t id = this->allocatedSize() - 1;
		while(!this->alive[id])
		{
			if(id == 0)
			{
				throw std::runtime_error(std::format("{} Pool: Can't use back() on empty Pool.", this->name));
			}
			id--; 
		}
		return this->objects[id]; 
	}

	T* data()
	{
		return this->objects.data();
	}

	const uint32_t size()
	{
		return this->elementCount; 
	}

	const size_t allocatedSize()
	{
		return this->objects.size();
	}

	const size_t sizeInBytes()
	{
		return this->objects.size() * sizeof(T);
	}

	
};


