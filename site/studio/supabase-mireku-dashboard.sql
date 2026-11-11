-- =====================================================================
--  MIREKU dashboard: the team list, visit counts, and the numbers
--  jamesmireku.com/admin shows.
--
--  Run once in Supabase -> SQL Editor. Safe to run again. Run it AFTER
--  supabase-mireku-interest.sql, and after both people below have made
--  an account (sign up at jamesmireku.com/account); the last query lists
--  who is on the team, so a missing account shows up at once.
--
--  SHARED PROJECT. Everything here is new and named mireku_*. Nothing
--  existing is changed: not profiles, not is_admin(), not purchases, not
--  the studio's own analytics. The MIREKU team is NOT made studio admin:
--  being on mireku_admins opens MIREKU's numbers and nothing else.
-- =====================================================================

-- ---------------------------------------------------------------------
--  1 · The MIREKU team
-- ---------------------------------------------------------------------
create table if not exists public.mireku_admins (
  user_id   uuid primary key references auth.users(id) on delete cascade,
  email     text not null,
  added_at  timestamptz not null default now()
);
alter table public.mireku_admins enable row level security;
revoke all on public.mireku_admins from anon, authenticated;   -- read only through the functions below

create or replace function public.is_mireku_admin()
returns boolean language sql security definer stable set search_path = '' as $$
  select exists (select 1 from public.mireku_admins where user_id = auth.uid());
$$;
revoke all on function public.is_mireku_admin() from public, anon;
grant execute on function public.is_mireku_admin() to authenticated;

-- The two people. Add someone later the same way; remove with a delete.
insert into public.mireku_admins (user_id, email)
select id, lower(email) from auth.users
 where lower(email) in ('amanorsac@gmail.com', 'j.mireku@icloud.com')
on conflict (user_id) do nothing;

-- ---------------------------------------------------------------------
--  2 · Visits to jamesmireku.com
--
--  Written by the site's own Worker (/api/hit), which adds the country
--  from the connection. No cookies, no names, no IP addresses: a page,
--  where the visitor came from, phone or computer, a country code, and a
--  random id that lives only as long as the browser tab. Visitors can
--  add rows and nothing else.
-- ---------------------------------------------------------------------
create table if not exists public.mireku_page_views (
  id          bigserial primary key,
  at          timestamptz not null default now(),
  path        text not null check (char_length(path) between 1 and 200),
  ref_host    text check (ref_host is null or char_length(ref_host) <= 120),
  device      text check (device in ('phone', 'tablet', 'computer')),
  country     text check (country is null or country ~ '^[A-Z]{2}$'),
  session_id  text not null check (char_length(session_id) between 8 and 64)
);
create index if not exists mireku_page_views_at on public.mireku_page_views (at desc);

create table if not exists public.mireku_events (
  id          bigserial primary key,
  at          timestamptz not null default now(),
  name        text not null check (name in ('notify_signup', 'buy_click', 'release_play', 'account_created')),
  detail      text check (detail is null or char_length(detail) <= 120),
  session_id  text not null check (char_length(session_id) between 8 and 64)
);
create index if not exists mireku_events_at on public.mireku_events (at desc);

alter table public.mireku_page_views enable row level security;
alter table public.mireku_events enable row level security;
drop policy if exists mireku_page_views_add on public.mireku_page_views;
create policy mireku_page_views_add on public.mireku_page_views for insert to anon, authenticated with check (true);
drop policy if exists mireku_events_add on public.mireku_events;
create policy mireku_events_add on public.mireku_events for insert to anon, authenticated with check (true);
revoke all on public.mireku_page_views, public.mireku_events from anon, authenticated;
grant insert (path, ref_host, device, country, session_id) on public.mireku_page_views to anon, authenticated;
grant insert (name, detail, session_id) on public.mireku_events to anon, authenticated;
grant usage on sequence public.mireku_page_views_id_seq, public.mireku_events_id_seq to anon, authenticated;

-- ---------------------------------------------------------------------
--  3 · What the dashboard reads. Team only; anyone else gets an error.
-- ---------------------------------------------------------------------
create or replace function public.mireku_dashboard(p_days int default 30)
returns jsonb language plpgsql security definer stable set search_path = '' as $$
declare
  d      int := greatest(1, least(coalesce(p_days, 30), 365));
  since  timestamptz := date_trunc('day', now()) - make_interval(days => d - 1);
  out    jsonb;
begin
  if not public.is_mireku_admin() then
    raise exception 'This is for the MIREKU team.' using errcode = '42501';
  end if;

  select jsonb_build_object(
    'days', d,
    'since', since,

    'visits', (select jsonb_build_object('views', count(*), 'visitors', count(distinct session_id))
                 from public.mireku_page_views where at >= since),

    'daily', (select coalesce(jsonb_agg(jsonb_build_object(
                       'day', to_char(g, 'YYYY-MM-DD'),
                       'visitors', coalesce(v.visitors, 0), 'views', coalesce(v.views, 0),
                       'signups', coalesce(i.n, 0)) order by g), '[]'::jsonb)
                from generate_series(since, date_trunc('day', now()), interval '1 day') g
                left join (select date_trunc('day', at) as day, count(*) views, count(distinct session_id) visitors
                             from public.mireku_page_views where at >= since group by 1) v on v.day = g
                left join (select date_trunc('day', created_at) as day, count(*) n
                             from public.mireku_interest where created_at >= since group by 1) i on i.day = g),

    'pages', (select coalesce(jsonb_agg(jsonb_build_object('name', path, 'n', n) order by n desc), '[]'::jsonb)
                from (select path, count(*) n from public.mireku_page_views where at >= since
                       group by path order by n desc limit 8) x),

    'sources', (select coalesce(jsonb_agg(jsonb_build_object('name', src, 'n', n) order by n desc), '[]'::jsonb)
                  from (select coalesce(nullif(ref_host, ''), 'Direct or unknown') src, count(distinct session_id) n
                          from public.mireku_page_views where at >= since group by 1 order by n desc limit 8) x),

    'countries', (select coalesce(jsonb_agg(jsonb_build_object('name', c, 'n', n) order by n desc), '[]'::jsonb)
                    from (select coalesce(country, '??') c, count(distinct session_id) n
                            from public.mireku_page_views where at >= since group by 1 order by n desc limit 8) x),

    'devices', (select coalesce(jsonb_agg(jsonb_build_object('name', dv, 'n', n) order by n desc), '[]'::jsonb)
                  from (select coalesce(device, 'unknown') dv, count(distinct session_id) n
                          from public.mireku_page_views where at >= since group by 1) x),

    'events', (select coalesce(jsonb_object_agg(name, n), '{}'::jsonb)
                 from (select name, count(*) n from public.mireku_events where at >= since group by name) x),

    'interest', (select jsonb_build_object(
                          'total', count(*),
                          'period', count(*) filter (where created_at >= since),
                          'week', count(*) filter (where created_at >= now() - interval '7 days'))
                   from public.mireku_interest),

    'sales', (select jsonb_build_object(
                       'paid', count(*) filter (where coalesce(kind, '') <> 'trial' and amount_cents > 0),
                       'free', count(*) filter (where coalesce(kind, '') <> 'trial' and amount_cents = 0),
                       'trials', count(*) filter (where kind = 'trial'),
                       'paid_period', count(*) filter (where coalesce(kind, '') <> 'trial' and amount_cents > 0 and purchased_at >= since))
                from public.purchases where app = 'mireku'),

    'revenue', (select coalesce(jsonb_agg(jsonb_build_object('currency', upper(currency), 'cents', total) order by total desc), '[]'::jsonb)
                  from (select currency, sum(amount_cents) total from public.purchases
                         where app = 'mireku' and coalesce(kind, '') <> 'trial' and amount_cents > 0
                         group by currency) x),

    'licences', jsonb_build_object(
        'keys', (select count(*) from public.purchases where app = 'mireku' and license_key is not null),
        'computers', (select count(*) from public.device_activations a
                        join public.purchases p on p.id = a.license_id
                       where p.app = 'mireku' and a.revoked_at is null))
  ) into out;
  return out;
end $$;
revoke all on function public.mireku_dashboard(int) from public, anon;
grant execute on function public.mireku_dashboard(int) to authenticated;

-- The people who asked to hear when MIREKU is out, newest first. Team only.
create or replace function public.mireku_interest_list()
returns table (created_at timestamptz, name text, phone text, email text)
language plpgsql security definer stable set search_path = '' as $$
begin
  if not public.is_mireku_admin() then
    raise exception 'This is for the MIREKU team.' using errcode = '42501';
  end if;
  return query select i.created_at, i.name, i.phone, i.email
                 from public.mireku_interest i order by i.created_at desc limit 5000;
end $$;
revoke all on function public.mireku_interest_list() from public, anon;
grant execute on function public.mireku_interest_list() to authenticated;

-- ---------------------------------------------------------------------
--  Who is on the team now. If someone is missing, they have no account
--  yet: they sign up at jamesmireku.com/account, then run this file again.
-- ---------------------------------------------------------------------
select email, added_at from public.mireku_admins order by email;
